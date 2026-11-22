#define _POSIX_C_SOURCE 200809L
#include "gauge_core.h"
#include "diagnostic_lease.h"
#include "gauge_coexistence.h"
#include "diag_link.h"
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


#include "most/dsi_client.h"
#include "most/most_link.h"
#include "f150_table.h"
#include "dme_signal_table.h"
#include "egs_signal_table.h"

#ifdef __QNX__
int boost_alert_tone(void);
#endif

static int trace_reads = 0;
static void pause_ms(int ms) { while (diag_running && poll(0, 0, ms) < 0 && errno == EINTR) {} }

static int decode_reply(unsigned ecu, unsigned did, const unsigned char *reply, int n, double *value,
                         const struct dme_signal_slot *scale) {
    int decoded = scale && scale->present
        ? bd_decode_scaled(scale->width, scale->is_signed, reply+3, (size_t)n-3,
                           scale->mul, scale->div, scale->add, scale->lo, scale->hi, value)
        : bd_decode_did(did,reply+3,(size_t)n-3,value);
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

/* 1 decoded, 0 unavailable or refused, -1 link lost, -3 this read failed but the link is kept.
 * scale, when its did is non-zero, is read instead of the given did and decoded with
 * bd_decode_scaled rather than gauge_core.c's dme8ff_r-default bd_decode_did. */
static int read_did(struct diag_link *link, unsigned ecu, unsigned did, double *value,
                     const struct dme_signal_slot *scale) {
    unsigned char request[3], prefix[3];
    unsigned char reply[4094];
    unsigned negative;
    int n;
    if (scale && scale->present) did = scale->did;
    request[0]=0x22; request[1]=(unsigned char)(did>>8); request[2]=(unsigned char)did;
    prefix[0]=0x62; prefix[1]=request[1]; prefix[2]=request[2];
    n=diag_link_request(link,ecu,request,3,prefix,3,reply,
                        diag_link_on_most(link) ? 64 : sizeof reply,500,&negative);
    if (n==-2) {
        fprintf(stderr,"ECU %02X DID %04X NRC %02X\n",ecu,did,negative);
        return 0;
    }
    if (n==-3) return -3;
    if (n<0) return -1;
    return decode_reply(ecu,did,reply,n,value,scale);
}

/* dids[] index of the EGS transmission-temperature read (DA12 by default; identify_egs may
 * replace both the DID and its decode, same as identify_dme does for the DME slots). */
enum { TRANS_SLOT = 5 };

/* Reads 22 F150 on the DME once per connection. A car reporting an SGBD that dme_signal_table.h has a
 * row for reads only that row's DIDs, with its scaling; a signal the row lacks is read from its PID where
 * there is one and is otherwise left blank, because nothing says which quantity the default DID would
 * return on that SGBD. Returns 1 then. An SGBD without a row, an unknown SGBD and a failed read all keep
 * the dme8ff_r default DIDs, with the PID fallbacks (see obd_pids).
 * slot_at maps a dme_signal_table.h slot to its dids[] index; TRANS_SLOT is never in this table,
 * only in egs_signal_table.h (identify_egs). Not read on a car. */
static int identify_dme(struct diag_link *link, struct dme_signal_slot *scale) {
    static const unsigned char slot_at[9] = {0,1,2,3,4,6,7,8,9};
    const unsigned char request[]={0x22,0xF1,0x50}, prefix[]={0x62,0xF1,0x50};
    unsigned char reply[16];
    unsigned negative, index, j;
    const char *sgbd;
    const struct dme_sgbd_row *row;
    int n=diag_link_request(link,0x12,request,3,prefix,3,reply,
                            diag_link_on_most(link) ? 16 : sizeof reply,500,&negative);
    if (n<6) return 0; /* the 62 F1 50 prefix plus at least a 3-byte index */
    index=((unsigned)reply[n-3]<<16)|((unsigned)reply[n-2]<<8)|reply[n-1];
    sgbd=f150_sgbd(0x12,index);
    row=dme_signal_row(sgbd);
    if (row) for (j=0; j<9; ++j) if (row->slot[j].present) scale[slot_at[j]]=row->slot[j];
    return row != 0;
}

/* Same identification, on the EGS (0x18), for egs_signal_table.h's transmission-temperature
 * overrides. Also runs over MOST: the tunnel forwards the EGS read (MOST.md). Test build only. */
static void identify_egs(struct diag_link *link, struct dme_signal_slot *scale) {
    const unsigned char request[]={0x22,0xF1,0x50}, prefix[]={0x62,0xF1,0x50};
    unsigned char reply[16];
    unsigned negative, index;
    const struct egs_sgbd_row *row;
    int n=diag_link_request(link,0x18,request,3,prefix,3,reply,sizeof reply,500,&negative);
    if (n<6) return;
    index=((unsigned)reply[n-3]<<16)|((unsigned)reply[n-2]<<8)|reply[n-1];
    row=egs_signal_row(f150_sgbd(0x18,index));
    if (!row || !row->trans_temp.present) return;
    scale[TRANS_SLOT].present=row->trans_temp.present; scale[TRANS_SLOT].width=row->trans_temp.width;
    scale[TRANS_SLOT].is_signed=row->trans_temp.is_signed; scale[TRANS_SLOT].did=row->trans_temp.did;
    scale[TRANS_SLOT].mul=row->trans_temp.mul; scale[TRANS_SLOT].div=row->trans_temp.div;
    scale[TRANS_SLOT].add=row->trans_temp.add; scale[TRANS_SLOT].lo=row->trans_temp.lo;
    scale[TRANS_SLOT].hi=row->trans_temp.hi;
}

/* Query selected standardized channels only. No engine type is needed. Where the identified SGBD has a
 * verified DID for a signal (dme_signal_table.h), that DID is read and the PID is only its fallback.
 * Otherwise four signals are read as standard Mode 01 PIDs first (coolant, intake air, rail pressure,
 * boost): the diesel DDEs reuse those proprietary DIDs for other quantities, so an unverified DID can
 * decode to a plausible wrong value. The DID is then read only while the car refuses the PID
 * (pid_refused). Timing and AFR keep their DID and fall back to the PID when it has no fresh value. */
enum { PID_ONLY, PID_FIRST, DID_FIRST };
static const struct {
    unsigned char pid;
    unsigned slot, mode;
} obd_pids[] = {
    {0x33, BD_BARO, 0}, {0x5c, BD_OIL_TEMP, 0}, {0x42, BD_BATTERY, 0},
    {0x0c, BD_RPM, 0}, {0x0d, BD_SPEED, 0}, {0x04, BD_LOAD, 0},
    {0x0b, BD_MAP, 0}, {0x10, BD_MAF, 0}, {0x11, BD_THROTTLE, 0},
    {0x46, BD_AMBIENT, 0}, {0x5e, BD_FUEL_RATE, 0},
    {0x2f, BD_FUEL_LEVEL, 0}, {0x49, BD_ACCELERATOR, 0},
    {0x05, BD_COOLANT, PID_FIRST}, {0x0f, BD_IAT, PID_FIRST}, {0x23, BD_RAIL, PID_FIRST},
    {0x0b, BD_BOOST, PID_FIRST}, {0x0e, BD_TIMING, DID_FIRST}, {0x24, BD_AFR, DID_FIRST},
    {0x0e, BD_SPARK_ADVANCE, 0}, {0x43, BD_ABS_LOAD, 0},
    {0x06, BD_STFT_B1, 0}, {0x07, BD_LTFT_B1, 0},
    {0x08, BD_STFT_B2, 0}, {0x09, BD_LTFT_B2, 0},
    {0x44, BD_LAMBDA_TARGET, 0}, {0x3c, BD_CAT_TEMP, 0},
    {0x4c, BD_THROTTLE_TARGET, 0}
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

/* PID 40 advertises support for 41 through 60. Some DMEs stay silent on
 * an unsupported fuel-rate request, which also interrupts other HSFZ reads. */
static int read_fuel_rate(struct diag_link *link, int *check_support, int *supported, double *value) {
    if (*check_support) {
        const unsigned char query[]={0x01,0x40}, prefix[]={0x41,0x40};
        unsigned char reply[16];
        unsigned negative;
        int n=diag_link_request(link,0x12,query,sizeof query,prefix,sizeof prefix,
                                reply,sizeof reply,500,&negative);
        *check_support=0;
        if (n==6) *supported=(reply[5] & 0x04)!=0;
        if (n==-1) return -1;
    }
    return *supported ? read_pid(link,0x5e,value) : 0;
}

static int send_frame(int sock, struct sockaddr_in *dest, bd_frame *f, uint64_t *observed) {
    char packet[BD_MAX_PACKET];
    uint64_t now = bd_mono_ms();
    unsigned i;
    int len;
    f->sent_ms=now;
    for (i = 0; i < BD_COUNT; ++i) {
        uint64_t age = now - observed[i];
        f->age_ms[i] = age > 999999 ? 999999 : (unsigned)age;
    }
    len = bd_encode(f, packet, sizeof packet);
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
    unsigned did_values = 0;
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
    /* scale[9] is the intercooler-coolant row slot, read apart from dids[] (see below). */
    struct dme_signal_slot scale[sizeof dids/sizeof dids[0] + 1] = {{0,0,0,0,0,0,0,0,0}};
    uint64_t next_ic = 0;
    int ethanol=0, diesel=0;
    unsigned char pid_refused[BD_COUNT] = {0};
    int no_default_dids = 0; /* the SGBD has a row: only its DIDs are read */
    const unsigned pid_first_slots = (1u<<BD_BOOST)|(1u<<BD_IAT)|(1u<<BD_RAIL)|(1u<<BD_COOLANT);
    int check_fuel_support=1, fuel_supported=1;
    uint64_t next_hud=0;
    unsigned selected=(1u << 8) - 1;
    uint64_t next_ethanol=0;
    uint64_t obd_due[sizeof obd_pids/sizeof obd_pids[0]] = {0};
    uint64_t obd_failed_until[sizeof obd_pids/sizeof obd_pids[0]] = {0};
    for (i = 1; i < (unsigned)argc; ++i) {
        if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--ethanol")) ethanol = 1;
        else if (!strcmp(argv[i], "--extended")) selected |= (1u << BD_OIL_TEMP) | (1u << BD_BATTERY);
        else if (!strcmp(argv[i], "--diesel")) diesel=1;
        else if (!strcmp(argv[i], "--signals") && i+1 < (unsigned)argc)
            selected=number(argv[++i],1,BD_ALL_MASK(BD_COUNT));
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
        else { fprintf(stderr, "Usage: %s (--host IPv4 | --demo) [--transport auto|hsfz|most] [--port 6801] [--udp-port 42860] [--cycles N] [--no-trans] [--trace] [--session]\n", argv[0]); return 2; }
    }
    if (host && demo) { fprintf(stderr, "Choose --host or --demo, never both.\n"); return 2; }
    if (!host && !demo && transport == DIAG_HSFZ) { fprintf(stderr, "--transport hsfz needs --host.\n"); return 2; }
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
    fprintf(stderr, "%s: 2016 340i candidate profile; validate against ISTA/logger.\n",
        demo ? "DEMO / NO VEHICLE CONNECTION" : host ? "READ ONLY / HSFZ" : "READ ONLY / MOST");
    while (diag_running && (!cycles || count < cycles) && (!session || getppid() == parent)) {
        uint64_t start = bd_mono_ms();
        f.seq++;
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
            f.valid=BD_ALL_MASK(BD_COUNT);
            f.value[BD_IC_COOLANT]=41+2*sin(t);
            for (i=0;i<BD_COUNT;++i) observed[i]=bd_mono_ms();
        } else {
            int full_scan = session ? !scanned : link.fd < 0 && !diag_link_on_most(&link);
            int up, stopped = 0; /* the DID scan ended early on a failed read */
            diag_link_tick(&link);
            up = diag_link_up(&link);
            if (up && full_scan) {
                no_default_dids = identify_dme(&link, scale);
                if (!no_trans) identify_egs(&link, scale);
            }
            for (i=0; up && i < sizeof dids / sizeof dids[0] && diag_running; ++i) {
                double v = 0;
                int rc;
                unsigned kept;
                if (diesel && i != TRANS_SLOT) continue;
                if (no_default_dids && i != TRANS_SLOT && !scale[i].present) continue;
                /* The PID answers for these four slots until the car refuses it. */
                if (i && (pid_first_slots & (1u << (i-1))) && !pid_refused[i-1] && !scale[i].present) continue;
                if (!(selected & (1u << (i ? i-1 : BD_BOOST)))) continue;
                if (no_trans && i == TRANS_SLOT) continue;
                if (start < failed_until[i]) continue;
                if (!full_scan && periods[i] && start < due[i]) continue;
                if (full_scan) due[i]=start+first_delay[i];
                else if (periods[i]) {
                    due[i]+=periods[i];
                    if (due[i]<=start) due[i]=start+periods[i];
                }
                /* Preserve values supplied by OBD while retrying the preferred DID. */
                kept = i ? f.valid & did_values & (1u << (i-1)) : 0;
                f.valid &= ~kept;
                rc = read_did(&link, i == TRANS_SLOT ? 0x18 : 0x12, dids[i], &v, &scale[i]);
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
                if (i == 1) {
                    if (!have_ambient || bd_mono_ms()-ambient_time > 1800) continue;
                    v = (v-ambient) * .145037738; /* kPa absolute - measured ambient -> gauge psi */
                }
                if (i == 2) v *= 14.7; /* measured lambda -> gasoline-equivalent AFR */
                did_values |= 1u << (i-1);
                f.value[i-1]=v; observed[i-1]=bd_mono_ms(); f.valid |= 1u << (i-1);
                /* A tunnel read is slow, so each new value goes out at once. */
                if (diag_link_on_most(&link) && !send_frame(sock,&dest,&f,observed)) { perror("send frame"); diag_running=0; }
            }
            /* Intercooler (low-temperature circuit) coolant: only an SGBD row names its DID, there is no PID. */
            if (up && !stopped && diag_running && !diesel && scale[9].present &&
                (selected & (1u<<BD_IC_COOLANT)) && start >= next_ic) {
                double v = 0;
                const int rc = read_did(&link,0x12,0,&v,&scale[9]);
                next_ic = bd_mono_ms()+2000;
                if (rc < 0 && rc != -3) {
                    diag_link_drop(&link);
                    if (diag_link_lost(&link)) { most_lost = 1; diag_running = 0; }
                    stopped = 1;
                } else if (rc == 1) {
                    f.value[BD_IC_COOLANT]=v; observed[BD_IC_COOLANT]=bd_mono_ms(); f.valid |= 1u<<BD_IC_COOLANT;
                } else f.valid &= ~(1u<<BD_IC_COOLANT);
            }
            for (i=0; up && !stopped && diag_running &&
                      i < sizeof obd_pids / sizeof obd_pids[0]; ++i) {
                const unsigned k = obd_pids[i].slot;
                double v = 0;
                int rc;
                /* Boost from Mode 01 is MAP minus the barometric reading. */
                const unsigned wanted = selected | ((selected & (1u<<BD_BOOST)) ? (1u<<BD_BARO) : 0);
                if (!(wanted & (1u<<k))) continue;
                /* DID first where a verified row names the DID; the PID otherwise (see the enum above). */
                const int did_first = obd_pids[i].mode == DID_FIRST ||
                                      (obd_pids[i].mode == PID_FIRST && scale[k+1].present);
                const int fallback = did_first && !diesel;
                if (fallback && (did_values & f.valid & (1u<<k)) &&
                    bd_mono_ms()-observed[k] < (k == BD_RAIL || k == BD_BOOST || k == BD_TIMING || k == BD_AFR ?
                                                BD_STALE_MS : BD_TEMP_STALE_MS)) continue;
                if (start < obd_due[i] || start < obd_failed_until[i]) continue;
                if (full_scan && i && !obd_due[i] && !fallback && !(obd_pids[i].mode == PID_FIRST && !did_first)) {
                    obd_due[i]=start+100+i*65;
                    continue;
                }
                obd_due[i]=start+2000;
                rc = k == BD_FUEL_RATE ? read_fuel_rate(&link,&check_fuel_support,&fuel_supported,&v) :
                                         read_pid(&link,obd_pids[i].pid,&v);
                if (rc == -3) { obd_failed_until[i]=bd_mono_ms()+read_retry_ms; continue; }
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
                pid_refused[k] = !rc && obd_pids[i].mode == PID_FIRST && !did_first;
                if (!rc) {
                    f.valid &= ~(1u<<k);
                    if (fallback || pid_refused[k]) obd_due[i]=bd_mono_ms()+10000; /* likely unsupported here */
                    continue;
                }
                did_values &= ~(1u << k);
                if (k == BD_BOOST) {
                    if (!(f.valid & (1u<<BD_BARO)) || bd_mono_ms()-observed[BD_BARO]>=BD_TEMP_STALE_MS)
                        continue;
                    v=(v-f.value[BD_BARO])*.145037738;
                }
                if (k == BD_AFR) v *= 14.7; /* lambda -> gasoline-equivalent AFR, as for the DID */
                f.value[k]=v; observed[k]=bd_mono_ms(); f.valid |= 1u << k;
                if (diag_link_on_most(&link) && !send_frame(sock,&dest,&f,observed)) { perror("send frame"); diag_running=0; }
            }
            // Supply RPM over the existing HSFZ connection only while HUD needs it.
            // Boost is shared from the dashboard's selected signals, with the same freshness rule.
            if (active >= 0 && start >= next_hud) {
                next_hud = bd_mono_ms() + 150;
                if (link.fd >= 0 && diag_running && gauge_active(HUD_TELEMETRY_PORT)) {
                    double rpm;
                    int read = read_pid(&link, 0x0c, &rpm);
                    struct hud_telemetry sample = { bd_mono_ms(), BOOST_ACTIVE_PORT, 0, 0, 0 };
                    if (read == 1) {
                        sample.rpm = (unsigned)rpm;
                        sample.valid = HUD_SAMPLE_RPM;
                    } else if (read == -1) diag_link_drop(&link);
                    if ((f.valid & (1u << BD_BOOST)) && sample.sampled_ms - observed[BD_BOOST] < 1100) {
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
                f.valid &= ~(1u<<BD_ETHANOL);
                if (n==38 && reply[17]<=100) {
                    observed[BD_ETHANOL]=bd_mono_ms();
                    f.value[BD_ETHANOL]=reply[17];
                    f.valid |= 1u<<BD_ETHANOL;
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
        if (!send_frame(sock,&dest,&f,observed)) { perror("send frame"); diag_running=0; }
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
