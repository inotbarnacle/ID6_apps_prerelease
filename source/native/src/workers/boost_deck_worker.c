#define _POSIX_C_SOURCE 200809L
#include "gauge_core.h"
#include "diagnostic_lease.h"
#include "gauge_coexistence.h"
#include "diag_link.h"
#include "obd_capabilities.h"
#include "boost_defaults.h"
#include "obd_egr.h"
#include "obd_pressure.h"
#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned number(const char *s, unsigned lo, unsigned hi) {
    char *end; unsigned long n = strtoul(s, &end, 10);
    if (!*s || *end || n < lo || n > hi) { fprintf(stderr, "Invalid number: %s\n", s); exit(2); }
    return (unsigned)n;
}

static uint64_t capability_token(const char *text) {
    char *end;
    unsigned long long value;
    if (*text<'0' || *text>'9') { fputs("Invalid capability token\n",stderr); exit(2); }
    errno=0; value=strtoull(text,&end,10);
    if (!value || *end || errno==ERANGE) { fputs("Invalid capability token\n",stderr); exit(2); }
    return (uint64_t)value;
}

static bd_mask signal_mask(const char *text) {
    char *end;
    unsigned long long value;
    if (*text<'0' || *text>'9') { fputs("Invalid signal mask\n",stderr); exit(2); }
    errno=0; value=strtoull(text,&end,10);
    if (!value || *end || errno==ERANGE || value>BD_ALL_MASK) {
        fputs("Invalid signal mask\n",stderr); exit(2);
    }
    return (bd_mask)value;
}


#include "most/dsi_client.h"
#include "most/most_link.h"

#ifdef __QNX__
int boost_alert_tone(void);
#endif

static int trace_reads = 0;
static void pause_ms(int ms) { while (diag_running && poll(0, 0, ms) < 0 && errno == EINTR) {} }

static int decode_reply(unsigned ecu, unsigned did, const unsigned char *reply, int n, double *value) {
    int decoded=bd_decode_did(did,reply+3,(size_t)n-3,value);
    int i;
    if (trace_reads) {
        fprintf(stderr,"RAW mono_ms=%llu ecu=%02X did=%04X payload=",
                (unsigned long long)bd_mono_ms(),ecu,did);
        for (i=3;i<n;++i) fprintf(stderr,"%02X",reply[i]);
        if (decoded) fprintf(stderr," decoded=%.6f\n",*value);
        else fprintf(stderr," unavailable%s\n",did==0x4a49 && n==5 &&
                     reply[3]==255 && reply[4]==255 ?
                     " reason=ambiguous-FFFF" : " reason=invalid-or-out-of-range");
    }
    return decoded;
}

/* 1 decoded, 0 unavailable or refused, -1 link lost, -3 this read failed but the link is kept. */
static int read_did(struct diag_link *link, unsigned ecu, unsigned did, double *value) {
    unsigned char request[]={0x22,(unsigned char)(did>>8),(unsigned char)did};
    unsigned char prefix[]={0x62,request[1],request[2]};
    unsigned char reply[4094];
    unsigned negative;
    int n=diag_link_request(link,ecu,request,3,prefix,3,reply,
                            diag_link_on_most(link) ? 64 : sizeof reply,500,&negative);
    if (n==-2) {
        fprintf(stderr,"ECU %02X DID %04X NRC %02X\n",ecu,did,negative);
        return 0;
    }
    if (n==-3) return -3;
    if (n<0) return -1;
    return decode_reply(ecu,did,reply,n,value);
}

/* Query selected standardized channels only. Diesel substitutes Mode 01
 * for unverified gasoline DIDs and reads the ambient reference before boost. */
static const struct {
    unsigned char pid;
    unsigned slot, diesel_only;
} obd_pids[] = {
    {0x33, BD_BARO, 0}, {0x5c, BD_OIL_TEMP, 0}, {0x42, BD_BATTERY, 0},
    {0x0c, BD_RPM, 0}, {0x0d, BD_SPEED, 0}, {0x04, BD_LOAD, 0},
    {0x0b, BD_MAP, 0}, {0x10, BD_MAF, 0}, {0x11, BD_THROTTLE, 0},
    {0x46, BD_AMBIENT, 0}, {0x5e, BD_FUEL_RATE, 0},
    {0x2f, BD_FUEL_LEVEL, 0}, {0x49, BD_ACCELERATOR, 0},
    {0x05, BD_COOLANT, 0}, {0x0f, BD_IAT, 1}, {0x23, BD_RAIL, 0},
    {0x0b, BD_BOOST, 1},
    {0x0e, BD_SPARK_ADVANCE, 0}, {0x43, BD_ABS_LOAD, 0},
    {0x06, BD_STFT_B1, 0}, {0x07, BD_LTFT_B1, 0},
    {0x08, BD_STFT_B2, 0}, {0x09, BD_LTFT_B2, 0},
    {0x44, BD_LAMBDA_TARGET, 0}, {0x3c, BD_CAT_TEMP, 0},
    {0x4c, BD_THROTTLE_TARGET, 0},
    {0x2c, BD_EGR_REQUEST, 0}, {0x2d, BD_EGR_ERROR, 0}
};

/* Same contract as read_did: 1 decoded, 0 unavailable or refused, -1 link lost, -3 kept. */
static int read_pid(struct diag_link *link, unsigned char pid, double *value) {
    unsigned char request[]={0x01,pid}, prefix[]={0x41,pid}, reply[16];
    unsigned negative;
    int decoded, j;
    int n=diag_link_request(link,0x12,request,2,prefix,2,reply,sizeof reply,500,&negative);
    if (n==-2) {
        fprintf(stderr,"ECU 12 PID %02X NRC %02X\n",pid,negative);
        return 0;
    }
    if (n==-3) return -3;
    if (n<0) return -1;
    decoded=n>=2 && bd_decode_obd(pid,reply+2,(size_t)n-2,value);
    if (trace_reads) {
        fprintf(stderr,"RAW mono_ms=%llu ecu=12 pid=%02X payload=",(unsigned long long)bd_mono_ms(),pid);
        for (j=2;j<n;++j) fprintf(stderr,"%02X",reply[j]);
        if (decoded) fprintf(stderr," decoded=%.6f\n",*value);
        else fprintf(stderr," unavailable reason=invalid-or-out-of-range\n");
    }
    return decoded;
}

static int read_egt(struct diag_link *link, unsigned bank, struct obd_egt_sample *sample) {
    const unsigned char request[]={1,(unsigned char)(0x78+bank)}, prefix[]={0x41,request[1]};
    unsigned char reply[16];
    unsigned negative;
    const int n=diag_link_request(link,0x12,request,2,prefix,2,reply,sizeof reply,500,&negative);
    if (n==-2) { fprintf(stderr,"ECU 12 PID %02X NRC %02X\n",request[1],negative); return 0; }
    if (n==-3) return -3;
    if (n<0) return -1;
    const int decoded=n>=2 && obd_egt_decode(reply+2,(size_t)n-2,sample);
    if (trace_reads) {
        fprintf(stderr,"RAW mono_ms=%llu ecu=12 pid=%02X payload=",(unsigned long long)bd_mono_ms(),request[1]);
        for (int i=2;i<n;++i) fprintf(stderr,"%02X",reply[i]);
        if (decoded) fprintf(stderr," present=%X valid=%X\n",sample->present,sample->valid);
        else fputs(" unavailable reason=invalid-layout\n",stderr);
    }
    return decoded;
}

static int read_egr(struct diag_link *link, struct obd_egr_sample *sample) {
    const unsigned char request[]={1,0x69}, prefix[]={0x41,0x69};
    unsigned char reply[16];
    unsigned negative;
    const int n=diag_link_request(link,0x12,request,2,prefix,2,reply,sizeof reply,500,&negative);
    if (n==-2) { fprintf(stderr,"ECU 12 PID 69 NRC %02X\n",negative); return 0; }
    if (n==-3) return -3;
    if (n<0) return -1;
    const int decoded=n>=2 && obd_egr_decode(reply+2,(size_t)n-2,sample);
    if (trace_reads) {
        fprintf(stderr,"RAW mono_ms=%llu ecu=12 pid=69 payload=",(unsigned long long)bd_mono_ms());
        for (int i=2;i<n;++i) fprintf(stderr,"%02X",reply[i]);
        if (decoded) fprintf(stderr," present=%X\n",sample->present);
        else fputs(" unavailable reason=invalid-layout\n",stderr);
    }
    return decoded;
}

static int read_pressure(struct diag_link *link, unsigned pid, struct obd_pressure_sample *sample) {
    const unsigned char request[]={1,(unsigned char)pid}, prefix[]={0x41,(unsigned char)pid};
    unsigned char reply[16];
    unsigned negative;
    const int n=diag_link_request(link,0x12,request,2,prefix,2,reply,sizeof reply,500,&negative);
    if (n==-2) { fprintf(stderr,"ECU 12 PID %02X NRC %02X\n",pid,negative); return 0; }
    if (n==-3) return -3;
    if (n<0) return -1;
    const int decoded=n>=2 && obd_pressure_decode(pid,reply+2,(size_t)n-2,sample);
    if (trace_reads) {
        fprintf(stderr,"RAW mono_ms=%llu ecu=12 pid=%02X payload=",(unsigned long long)bd_mono_ms(),pid);
        for (int i=2;i<n;++i) fprintf(stderr,"%02X",reply[i]);
        if (decoded) fprintf(stderr," present=%X\n",sample->present);
        else fputs(" unavailable reason=invalid-layout\n",stderr);
    }
    return decoded;
}

static int send_frame(int sock, struct sockaddr_in *dest, bd_frame *f, uint64_t *observed,
                      uint64_t auto_token, const struct obd_capabilities *caps, int trans_observed,
                      const struct obd_egt_evidence *egt) {
    char packet[BD_AUTO_PACKET];
    uint64_t now = bd_mono_ms();
    unsigned i;
    int len;
    f->sent_ms=now;
    for (i = 0; i < BD_COUNT; ++i) {
        uint64_t age = now - observed[i];
        f->age_ms[i] = age > 999999 ? 999999 : (unsigned)age;
    }
    if (auto_token) {
        bd_cap_frame report;
        bd_mask known, supported;
        report.token=auto_token; report.worker_pid=(uint32_t)getpid(); report.sent_ms=now;
        bd_diesel_masks(caps,trans_observed,egt,&known,&supported);
        report.known=known; report.supported=supported;
        len=bd_auto_encode(&report,f,packet,sizeof packet);
    } else len = bd_encode(f, packet, BD_MAX_PACKET);
    return len > 0 && sendto(sock, packet, (size_t)len, 0,
        (struct sockaddr *)dest, sizeof *dest) == len;
}
int main(int argc, char **argv) {
#ifdef __QNX__
    if (argc==2 && !strcmp(argv[1],"--alert-tone")) return boost_alert_tone();
#endif
    const char *host = 0;
    unsigned port = 6801, udp_port = BD_UDP_PORT, cycles = 0, count = 0, i;
    int demo = 0, sock, active = -1, no_trans = 0, session = 0, scanned = 0, transport = DIAG_AUTO, most_lost = 0;
    struct diag_link link;
    struct diag_link_cfg cfg;
    pid_t parent = getppid();
    struct sockaddr_in dest;
    bd_frame f;
    uint64_t observed[BD_COUNT] = {0};
    double ambient = 0;
    uint64_t ambient_time = 0;
    int have_ambient = 0;
    bd_mask did_values = 0;
    const unsigned tick_ms = 35;
    const unsigned minimum_idle_ms = 3;
    const unsigned read_retry_ms = 2000;
    /* Each slow signal has its own deadline. Stagger temperature reads
     * after the first scan so they do not delay the fast gauges together. */
    const unsigned dids[] = {0x4201,0x4205,0x5889,0x4a49,0x580f,0xda12,0x56d7,0x586f,0x4300};
    const unsigned periods[] = {1000,0,0,0,2000,2000,0,0,2000};
    const unsigned first_delay[] = {900,0,0,0,500,1150,0,0,1700};
    uint64_t due[sizeof dids/sizeof dids[0]] = {0};
    uint64_t failed_until[sizeof dids/sizeof dids[0]] = {0};
    int ethanol=0, diesel=0;
    struct obd_capabilities capabilities = {{0}, {0}, 0};
    struct obd_egt_evidence egt = {0,{0}};
    uint64_t egt_due[OBD_EGT_BANKS]={0}, egt_failed_until[OBD_EGT_BANKS]={0};
    uint64_t egr_due=0, egr_failed_until=0;
    uint64_t pressure_due[OBD_PRESSURE_GROUPS]={0}, pressure_failed_until[OBD_PRESSURE_GROUPS]={0};
    uint64_t auto_token=0;
    int trans_observed=0, explicit_signals=0;
    uint64_t next_hud=0;
    bd_mask selected=(UINT64_C(1) << 8) - 1;
    uint64_t next_ethanol=0;
    uint64_t obd_due[sizeof obd_pids/sizeof obd_pids[0]] = {0};
    uint64_t obd_failed_until[sizeof obd_pids/sizeof obd_pids[0]] = {0};
    for (i = 1; i < (unsigned)argc; ++i) {
        if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--ethanol")) ethanol = 1;
        else if (!strcmp(argv[i], "--extended")) selected |= (UINT64_C(1) << BD_OIL_TEMP) | (UINT64_C(1) << BD_BATTERY);
        else if (!strcmp(argv[i], "--diesel")) diesel=1;
        else if (!strcmp(argv[i], "--auto-diesel") && i+1 < (unsigned)argc) {
            auto_token=capability_token(argv[++i]); diesel=1;
        }
        else if (!strcmp(argv[i], "--signals") && i+1 < (unsigned)argc) {
            selected=signal_mask(argv[++i]); explicit_signals=1;
        }
        else if (!strcmp(argv[i], "--session")) session = 1;
        else if (!strcmp(argv[i], "--trace")) trace_reads = 1;
        else if (!strcmp(argv[i], "--no-trans")) no_trans = 1;
        else if (!strcmp(argv[i], "--host") && i+1 < (unsigned)argc) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i+1 < (unsigned)argc) port = number(argv[++i],1,65535);
        else if (!strcmp(argv[i], "--udp-port") && i+1 < (unsigned)argc) udp_port = number(argv[++i],1,65535);
        else if (!strcmp(argv[i], "--cycles") && i+1 < (unsigned)argc) cycles = number(argv[++i],1,1000000);
        else if (!strcmp(argv[i], "--transport") && i+1 < (unsigned)argc && !strcmp(argv[i+1], "auto")) ++i;
        else if (!strcmp(argv[i], "--transport") && i+1 < (unsigned)argc && !strcmp(argv[i+1], "hsfz")) { transport = DIAG_HSFZ; ++i; }
        else if (!strcmp(argv[i], "--transport") && i+1 < (unsigned)argc && !strcmp(argv[i+1], "most")) { transport = DIAG_MOST; ++i; }
        else { fprintf(stderr, "Usage: %s (--host IPv4 | --demo) [--auto-diesel TOKEN | --signals MASK] [--diesel] [--transport auto|hsfz|most] [--port 6801] [--udp-port 42860] [--cycles N] [--no-trans] [--trace] [--session]\n", argv[0]); return 2; }
    }
    if (auto_token && (demo || explicit_signals)) {
        fputs("Automatic diesel gauges require live read-only transport and no --signals override.\n",stderr);
        return 2;
    }
    if (host && demo) { fprintf(stderr, "Choose --host or --demo, never both.\n"); return 2; }
    if (!host && !demo && transport == DIAG_HSFZ) { fprintf(stderr, "--transport hsfz needs --host.\n"); return 2; }
    if (native_android_auto_installed()) transport = DIAG_HSFZ;
    if (transport == DIAG_MOST) host = 0;
    memset(&cfg, 0, sizeof cfg);
    cfg.host = host; cfg.port = port; cfg.transport = transport; cfg.kind = DIAG_LOOP;
    cfg.dsi_name = "boostdeck"; cfg.session = session;
    diag_link_signals();
    if (trace_reads) most_log_level(MOST_LOG_I);
    if (session && (parent <= 1 || demo)) return 2;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("UDP socket"); return 1; }
    if (!demo) {
        active = gauge_bind(BOOST_ACTIVE_PORT);
        if (active < 0) {
            if (errno != EADDRINUSE) { perror("BoostDeck HUD sharing socket"); close(sock); return 1; }
            fputs("BoostDeck HUD sharing port already in use. Collecting gauges without HUD sharing.\n", stderr);
            session = cfg.session = 1; /* arbitrate diagnostic traffic even without the HUD marker */
        }
    }
    diag_link_init(&link, &cfg);
    memset(&dest,0,sizeof dest); dest.sin_family=AF_INET; dest.sin_port=htons((uint16_t)udp_port);
    inet_pton(AF_INET,"127.0.0.1",&dest.sin_addr); /* local process IPC only */
    memset(&f,0,sizeof f); f.demo=(unsigned)demo;
    fprintf(stderr, "%s: %s\n",
        demo ? "DEMO / NO VEHICLE CONNECTION" : host ? "READ ONLY / HSFZ" : "READ ONLY / MOST",
        diesel ? "standard OBD capability profile; EGS candidate requires validation." :
                 "2016 340i candidate profile; validate against ISTA/logger.");
    while (diag_running && (!cycles || count < cycles) && (!session || getppid() == parent)) {
        uint64_t start = bd_mono_ms();
        f.seq++;
        if (auto_token) {
            bd_mask known, supported;
            unsigned char slots[BD_DEFAULT_SLOTS];
            bd_diesel_masks(&capabilities,trans_observed,&egt,&known,&supported);
            bd_diesel_slots(supported,slots);
            /* Keep unknown candidates eligible for bounded discovery. A missing
             * measurement is never used to classify its PID as unsupported. */
            selected=bd_slots_mask(slots) | (bd_diesel_candidates() & ~known);
        }
        if (demo) {
            double t = count * (tick_ms / 1000.0);
            f.value[0]=12+8*sin(t); f.value[1]=13.2+.6*sin(t);
            f.value[2]=8+3*sin(t); f.value[3]=44+2*sin(t); f.value[4]=88;
            f.value[BD_RAIL]=95+22*sin(t); f.value[BD_OIL]=2.5+.5*sin(t);
            f.value[BD_COOLANT]=86+3*sin(t);
            f.value[BD_ETHANOL]=38;
            f.value[BD_OIL_TEMP]=98+4*sin(t); f.value[BD_BATTERY]=14.2;
            f.value[BD_RPM]=2800; f.value[BD_SPEED]=80; f.value[BD_LOAD]=53;
            f.value[BD_MAP]=185; f.value[BD_MAF]=62; f.value[BD_THROTTLE]=45;
            f.value[BD_BARO]=101; f.value[BD_AMBIENT]=23; f.value[BD_FUEL_RATE]=14;
            f.value[BD_FUEL_LEVEL]=69; f.value[BD_ACCELERATOR]=48;
            f.value[BD_SPARK_ADVANCE]=12; f.value[BD_ABS_LOAD]=94;
            f.value[BD_STFT_B1]=2.3; f.value[BD_LTFT_B1]=-1.5;
            f.value[BD_STFT_B2]=1.6; f.value[BD_LTFT_B2]=-0.8;
            f.value[BD_LAMBDA_TARGET]=.95; f.value[BD_CAT_TEMP]=580;
            f.value[BD_THROTTLE_TARGET]=46;
            for (unsigned sensor=BD_EGT_B1S1;sensor<BD_WIRE8_COUNT;++sensor)
                f.value[sensor]=400+10*(sensor-BD_EGT_B1S1)+20*sin(t);
            for (unsigned signal=BD_EGR_REQUEST;signal<=BD_EGR_B_ERROR;++signal)
                f.value[signal]=signal==BD_EGR_ERROR || signal==BD_EGR_A_ERROR || signal==BD_EGR_B_ERROR ? -12.5 : 40;
            for (unsigned signal=BD_RAIL_A_REQUEST;signal<=BD_BOOST_B_ACTUAL;++signal)
                f.value[signal]=signal<=BD_RAIL_B_ACTUAL ? 1200 : 200;
            f.valid=BD_LEGACY_MASK | (selected & ~BD_LEGACY_MASK);
            for (i=0;i<BD_COUNT;++i) observed[i]=bd_mono_ms();
        } else {
            int full_scan = session ? !scanned : link.fd < 0 && !diag_link_on_most(&link);
            int up, stopped = 0; /* the DID scan ended early on a failed read */
            diag_link_tick(&link);
            up = diag_link_up(&link);
            if (diag_link_on_most(&link)) f.valid &= ~(UINT64_C(1) << BD_TRANS); /* no EGS reads on MOST */
            for (i=0; up && i < sizeof dids / sizeof dids[0] && diag_running; ++i) {
                double v = 0;
                int rc;
                unsigned kept;
                int on_most = diag_link_on_most(&link);
                if (diesel && dids[i] != 0xda12) continue;
                if (!(selected & (UINT64_C(1) << (i ? i-1 : BD_BOOST)))) continue;
                if ((no_trans || on_most) && dids[i] == 0xda12) continue;
                if (start < failed_until[i]) continue;
                if (!full_scan && periods[i] && start < due[i]) continue;
                if (full_scan) due[i]=start+first_delay[i];
                else if (periods[i]) {
                    due[i]+=periods[i];
                    if (due[i]<=start) due[i]=start+periods[i];
                }
                /* Preserve values supplied by OBD while retrying the preferred DID. */
                kept = i ? f.valid & did_values & (UINT64_C(1) << (i-1)) : 0;
                f.valid &= ~kept;
                rc = read_did(&link, dids[i] == 0xda12 ? 0x18 : 0x12, dids[i], &v);
                if (rc == -3) { /* skip it for a while; a value still fresh stays shown */
                    failed_until[i] = bd_mono_ms()+read_retry_ms;
                    if (kept) {
                        uint64_t age = bd_mono_ms()-observed[i-1];
                        f.age_ms[i-1] = age > 999999 ? 999999 : (unsigned)age;
                        f.valid |= kept;
                        if (!bd_is_fresh(&f, i-1, 0)) f.valid &= ~kept;
                    }
                    continue;
                }
                if (rc < 0) {
                    diag_link_drop(&link);
                    if (diag_link_lost(&link)) { most_lost = 1; diag_running = 0; }
                    else if (session) failed_until[i]=bd_mono_ms()+read_retry_ms;
                    else {
                        f.valid=0;
                        have_ambient=0;
                    }
                    stopped = 1;
                    break;
                }
                if (!rc) failed_until[i]=bd_mono_ms()+read_retry_ms;
                if (i == 0) {
                    have_ambient=rc != 0;
                    if (have_ambient) { ambient=v; ambient_time=bd_mono_ms(); }
                    continue;
                }
                if (!rc) continue;
                if (dids[i]==0xda12) trans_observed=1;
                if (i == 1) {
                    if (!have_ambient || bd_mono_ms()-ambient_time > 1800) continue;
                    v = (v-ambient) * .145037738; /* kPa absolute - measured ambient -> gauge psi */
                }
                if (i == 2) v *= 14.7; /* measured lambda -> gasoline-equivalent AFR */
                did_values |= UINT64_C(1) << (i-1);
                f.value[i-1]=v; observed[i-1]=bd_mono_ms(); f.valid |= UINT64_C(1) << (i-1);
                /* A tunnel read is slow, so each new value goes out at once. */
                if (diag_link_on_most(&link) && !send_frame(sock,&dest,&f,observed,auto_token,&capabilities,trans_observed,&egt)) { perror("send frame"); diag_running=0; }
            }
            for (i=0; up && !stopped && diag_running &&
                      i < sizeof obd_pids / sizeof obd_pids[0]; ++i) {
                const unsigned k = obd_pids[i].slot;
                double v = 0;
                int rc;
                const bd_mask wanted = selected | (diesel && (selected & (UINT64_C(1)<<BD_BOOST)) ?
                                                         (UINT64_C(1)<<BD_BARO) : 0);
                if (!(wanted & (UINT64_C(1)<<k)) || (obd_pids[i].diesel_only && !diesel)) continue;
                const int fallback = !diesel && (k == BD_COOLANT || k == BD_RAIL);
                if (fallback && (did_values & f.valid & (UINT64_C(1)<<k)) &&
                    bd_mono_ms()-observed[k] < (k == BD_RAIL ? BD_STALE_MS : BD_TEMP_STALE_MS)) continue;
                if (start < obd_due[i] || start < obd_failed_until[i]) continue;
                if (full_scan && i && !obd_due[i] && !fallback) {
                    obd_due[i]=start+100+i*65;
                    continue;
                }
                obd_due[i]=start+2000;
                /* A DDE may reset the connection on an unsupported PID, including
                 * oil temperature. Never probe an unadvertised diesel channel.
                 * Keep legacy gasoline fixed-DID/fallback selection unchanged. */
                const int egr_scalar=k==BD_EGR_REQUEST || k==BD_EGR_ERROR;
                rc = diesel || egr_scalar || k == BD_OIL_TEMP || k == BD_FUEL_RATE ?
                     obd_pid_advertised(&capabilities,&link,obd_pids[i].pid) : 1;
                if (rc == OBD_CAPABILITY_PENDING) {
                    obd_due[i] = bd_mono_ms()+100;
                    continue;
                }
                if (rc == 1) {
                    if (egr_scalar) obd_due[i]=bd_mono_ms()+2000;
                    rc = read_pid(&link,obd_pids[i].pid,&v);
                }
                if (rc == -3) { obd_failed_until[i]=bd_mono_ms()+(egr_scalar ? OBD_EGR_RETRY_MS : read_retry_ms); continue; }
                if (rc < 0) {
                    diag_link_drop(&link);
                    if (diag_link_lost(&link)) { most_lost = 1; diag_running = 0; }
                    else if (session) obd_failed_until[i]=bd_mono_ms()+read_retry_ms;
                    else {
                        f.valid=0;
                        have_ambient=0;
                    }
                    break;
                }
                if (!rc) {
                    f.valid &= ~(UINT64_C(1)<<k);
                    if (egr_scalar) obd_failed_until[i]=bd_mono_ms()+OBD_EGR_RETRY_MS;
                    continue;
                }
                /* The 8-bit standard MAP saturates at 255 kPa absolute. It
                 * cannot establish the real diesel manifold pressure there. */
                if (diesel && (k == BD_BOOST || k == BD_MAP) && v >= 255) {
                    f.valid &= ~(UINT64_C(1)<<k);
                    continue;
                }
                did_values &= ~(UINT64_C(1) << k);
                if (k == BD_BOOST) {
                    if (!(f.valid & (UINT64_C(1)<<BD_BARO)) || bd_mono_ms()-observed[BD_BARO]>=BD_TEMP_STALE_MS)
                        continue;
                    v=(v-f.value[BD_BARO])*.145037738;
                }
                f.value[k]=v; observed[k]=bd_mono_ms(); f.valid |= UINT64_C(1) << k;
                if (diag_link_on_most(&link) && !send_frame(sock,&dest,&f,observed,auto_token,&capabilities,trans_observed,&egt)) { perror("send frame"); diag_running=0; }
            }
            /* One request serves the selected sensors in a bank. PID advertisement
             * alone is insufficient: retain separate evidence of the sensor bits. */
            for (unsigned bank=0;up && bank<OBD_EGT_BANKS && diag_running;++bank) {
                const unsigned first=BD_EGT_B1S1+bank*OBD_EGT_SENSORS;
                const bd_mask bank_mask=(BD_BIT(OBD_EGT_SENSORS)-1)<<first;
                const bd_mask wanted=selected & bank_mask;
                if (!wanted || start<egt_due[bank] || start<egt_failed_until[bank]) continue;
                if (diag_link_lost(&link) || (!diag_link_on_most(&link) && link.fd<0)) break;
                egt_due[bank]=start+2000;
                int rc=obd_pid_advertised(&capabilities,&link,0x78+bank);
                if (rc==OBD_CAPABILITY_PENDING) { egt_due[bank]=bd_mono_ms()+100; continue; }
                struct obd_egt_sample sample;
                if (rc==1) {
                    /* Earlier reads may have consumed much of this loop. Bound
                     * bank traffic from the actual request, not the loop start. */
                    egt_due[bank]=bd_mono_ms()+2000;
                    rc=read_egt(&link,bank,&sample);
                }
                if (rc==-3) { egt_failed_until[bank]=bd_mono_ms()+OBD_EGT_RETRY_MS; continue; }
                if (rc<0) {
                    f.valid &= ~bank_mask;
                    diag_link_drop(&link);
                    if (diag_link_lost(&link)) { most_lost=1; diag_running=0; }
                    else if (session) egt_failed_until[bank]=bd_mono_ms()+OBD_EGT_RETRY_MS;
                    else { f.valid=0; have_ambient=0; }
                    break;
                }
                f.valid &= ~bank_mask;
                if (!rc) { egt_failed_until[bank]=bd_mono_ms()+OBD_EGT_RETRY_MS; continue; }
                if ((egt.known & (1u<<bank)) && egt.supported[bank]!=sample.present) {
                    /* Capability evidence is immutable for this worker/endpoint.
                     * A conflicting sensor layout cannot refresh measurements. */
                    egt_failed_until[bank]=bd_mono_ms()+OBD_EGT_RETRY_MS;
                    continue;
                }
                egt.known |= 1u<<bank;
                egt.supported[bank]=sample.present;
                for (unsigned sensor=0;sensor<OBD_EGT_SENSORS;++sensor) {
                    const unsigned signal=first+sensor;
                    if (!(wanted & BD_BIT(signal)) || !(sample.valid & (1u<<sensor))) continue;
                    f.value[signal]=sample.celsius[sensor]; observed[signal]=bd_mono_ms();
                    f.valid |= BD_BIT(signal);
                }
                if (diag_link_on_most(&link) && !send_frame(sock,&dest,&f,observed,auto_token,&capabilities,trans_observed,&egt)) {
                    perror("send frame"); diag_running=0;
                }
            }
            /* PID 69 serves its six selected fields with one read. No derived
             * actual value or fallback between unspecified and A/B fields. */
            const bd_mask egr_mask=(BD_BIT(OBD_EGR_FIELDS)-1)<<BD_EGR_A_REQUEST;
            if (up && diag_running && (selected & egr_mask) && start>=egr_due && start>=egr_failed_until &&
                !diag_link_lost(&link) && (diag_link_on_most(&link) || link.fd>=0)) {
                egr_due=bd_mono_ms()+2000;
                int rc=obd_pid_advertised(&capabilities,&link,0x69);
                if (rc==OBD_CAPABILITY_PENDING) egr_due=bd_mono_ms()+100;
                else {
                    struct obd_egr_sample sample;
                    if (rc==1) { egr_due=bd_mono_ms()+2000; rc=read_egr(&link,&sample); }
                    if (rc==-3) egr_failed_until=bd_mono_ms()+OBD_EGR_RETRY_MS;
                    else {
                        f.valid &= ~egr_mask;
                        if (rc<0) {
                            diag_link_drop(&link);
                            if (diag_link_lost(&link)) { most_lost=1; diag_running=0; }
                            else if (session) egr_failed_until=bd_mono_ms()+OBD_EGR_RETRY_MS;
                            else { f.valid=0; have_ambient=0; }
                        } else if (!rc) egr_failed_until=bd_mono_ms()+OBD_EGR_RETRY_MS;
                        else {
                            for (unsigned field=0;field<OBD_EGR_FIELDS;++field) {
                                const unsigned signal=BD_EGR_A_REQUEST+field;
                                if (!(selected & BD_BIT(signal)) || !(sample.present & (1u<<field))) continue;
                                f.value[signal]=sample.value[field]; observed[signal]=bd_mono_ms();
                                f.valid |= BD_BIT(signal);
                            }
                            if (diag_link_on_most(&link) && !send_frame(sock,&dest,&f,observed,auto_token,&capabilities,trans_observed,&egt)) {
                                perror("send frame"); diag_running=0;
                            }
                        }
                    }
                }
            }
            /* Separate pressure groups preserve requested/actual and reference
             * meanings. Neither replaces MAP, derived boost or legacy rail. */
            for (unsigned group=0;up && diag_running && group<OBD_PRESSURE_GROUPS;++group) {
                const unsigned pid=group==0 ? 0x6d : 0x70;
                const unsigned first=BD_RAIL_A_REQUEST+group*OBD_PRESSURE_FIELDS;
                const bd_mask group_mask=(BD_BIT(OBD_PRESSURE_FIELDS)-1)<<first;
                const uint64_t now=bd_mono_ms();
                if (!(selected & group_mask) || now<pressure_due[group] || now<pressure_failed_until[group]) continue;
                if (diag_link_lost(&link) || (!diag_link_on_most(&link) && link.fd<0)) break;
                pressure_due[group]=now+2000;
                int rc=obd_pid_advertised(&capabilities,&link,pid);
                if (rc==OBD_CAPABILITY_PENDING) { pressure_due[group]=bd_mono_ms()+100; continue; }
                struct obd_pressure_sample sample;
                if (rc==1) {
                    pressure_due[group]=bd_mono_ms()+2000;
                    rc=read_pressure(&link,pid,&sample);
                }
                if (rc==-3) { pressure_failed_until[group]=bd_mono_ms()+OBD_PRESSURE_RETRY_MS; continue; }
                f.valid &= ~group_mask;
                if (rc<0) {
                    diag_link_drop(&link);
                    if (diag_link_lost(&link)) { most_lost=1; diag_running=0; }
                    else if (session) pressure_failed_until[group]=bd_mono_ms()+OBD_PRESSURE_RETRY_MS;
                    else { f.valid=0; have_ambient=0; }
                    break;
                }
                if (!rc) { pressure_failed_until[group]=bd_mono_ms()+OBD_PRESSURE_RETRY_MS; continue; }
                for (unsigned field=0;field<OBD_PRESSURE_FIELDS;++field) {
                    const unsigned signal=first+field;
                    if (!(selected & BD_BIT(signal)) || !(sample.present & (1u<<field))) continue;
                    f.value[signal]=sample.value[field]; observed[signal]=bd_mono_ms();
                    f.valid |= BD_BIT(signal);
                }
                if (diag_link_on_most(&link) && !send_frame(sock,&dest,&f,observed,auto_token,&capabilities,trans_observed,&egt)) {
                    perror("send frame"); diag_running=0;
                }
            }
            // Supply RPM over the existing HSFZ connection only while HUD needs it.
            // Boost is shared from the dashboard's selected signals, with the same freshness rule.
            if (active >= 0 && start >= next_hud) {
                next_hud = bd_mono_ms() + 150;
                if (link.fd >= 0 && diag_running && gauge_active(HUD_TELEMETRY_PORT)) {
                    double rpm;
                    int read = diesel ? obd_pid_advertised(&capabilities,&link,0x0c) : 1;
                    if (read==1) read=read_pid(&link, 0x0c, &rpm);
                    struct hud_telemetry sample = { bd_mono_ms(), BOOST_ACTIVE_PORT, 0, 0, 0 };
                    if (read == 1) {
                        sample.rpm = (unsigned)rpm;
                        sample.valid = HUD_SAMPLE_RPM;
                    } else if (read == -1) diag_link_drop(&link);
                    if ((f.valid & (UINT64_C(1) << BD_BOOST)) && sample.sampled_ms - observed[BD_BOOST] < 1100) {
                        sample.boost_tenths = (int)round(f.value[BD_BOOST] * 10);
                        sample.valid |= HUD_SAMPLE_BOOST;
                    }
                    gauge_publish(active, &sample);
                }
            }
            /* Not over MOST: the tunnel's echo check and 2 s wait do not suit this read. */
            if (up && link.fd>=0 && ethanol && !diesel && start>=next_ethanol && diag_running) {
                /* Published MHD+ MG1 monitor layout. This is the tune's active
                 * ethanol percentage, distinct from the raw ECA sensor. */
                const unsigned char query[]={0x2c,0x88,0x01}, prefix[]={0x6c,0x88};
                unsigned char reply[38];
                unsigned negative;
                const int n=diag_link_request(&link,0x12,query,sizeof query,prefix,sizeof prefix,
                                              reply,sizeof reply,180,&negative);
                f.valid &= ~(UINT64_C(1)<<BD_ETHANOL);
                if (n==38 && reply[17]<=100) {
                    observed[BD_ETHANOL]=bd_mono_ms();
                    f.value[BD_ETHANOL]=reply[17];
                    f.valid |= UINT64_C(1)<<BD_ETHANOL;
                    next_ethanol=observed[BD_ETHANOL]+5000;
                } else {
                    next_ethanol=bd_mono_ms()+30000;
                    if (n==-1) diag_link_drop(&link);
                }
            }
            if (diag_link_on_most(&link)) scanned=1;
            else if (link.fd>=0) {
                scanned=1;
                (void)diag_link_should_yield(&link,bd_mono_ms());
            }
        }
        if (!send_frame(sock,&dest,&f,observed,auto_token,&capabilities,trans_observed,&egt)) { perror("send frame"); diag_running=0; }
        ++count;
        {
            uint64_t elapsed = bd_mono_ms()-start;
            unsigned interval = session && link.fd<0 && !diag_link_on_most(&link) ? 55 : tick_ms;
            unsigned idle_ms = elapsed < interval ? (unsigned)(interval-elapsed) : 0;
            if (!demo && idle_ms < minimum_idle_ms) idle_ms = minimum_idle_ms;
            if (idle_ms) pause_ms((int)idle_ms);
        }
    }
    diag_link_close(&link);
    close(sock);
    if (active >= 0) close(active);
    return most_lost ? 1 : 0;
}
