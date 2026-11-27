#define _POSIX_C_SOURCE 200809L
#include "gauge_core.h"
#include "gauge_coexistence.h"
#include "diagnostic_lease.h"
#include "diag_link.h"
#include "n57.h"
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



#include "drive_frame.h"

struct drive_n57_exchange {
    struct diag_link *link;
    uint64_t sampled_ms;
};

static int n57_link_exchange(void *ctx, unsigned ecu, const unsigned char *req, unsigned n,
                              const unsigned char *prefix, unsigned pn, unsigned char *reply,
                              unsigned cap, unsigned timeout, unsigned *negative) {
    struct drive_n57_exchange *sample=ctx;
    /* Clear/define and identity do not sample values. Date the actual read before waiting for it. */
    if (n==3 && !memcmp(req,"\x22\xf3\x03",3)) sample->sampled_ms=bd_mono_ms();
    return diag_link_request(sample->link,ecu,req,n,prefix,pn,reply,cap,timeout,negative);
}

/* The raw PID value, -1 when the ECU refused it. Returns 0 read, -1 link lost, -3 skip this frame. */
static int read_live_pid(struct diag_link *link, unsigned char pid, int width, int *value) {
    unsigned char req[]={0x01,pid}, prefix[]={0x41,pid}, reply[4];
    unsigned negative;
    int n=diag_link_request(link,0x12,req,sizeof req,prefix,sizeof prefix,reply,sizeof reply,250,&negative);
    *value=-1;
    if (n==-1 || n==-3) return n;
    if (n==width+2) *value=width==1 ? reply[2] : ((unsigned)reply[2]*256+reply[3])/4;
    return 0;
}

int main(int argc, char **argv) {
    struct diag_link_cfg cfg={0,6801,DIAG_AUTO,DIAG_LOOP,"shiftdeck",1,0};
    struct diag_link link;
    unsigned udp_port=DRIVE_PORT, cycles=0, count=0;
    pid_t parent=getppid();
    struct sockaddr_in dest;
    int sock, active, i, lost, speed_only=0;
    unsigned source;
    unsigned n57_index=0;
    if (n57_settings(&n57_index)<0) return 2;
    uint64_t speed_due=0;
    int last_speed=-1;
    for (i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && i+1<argc && !cfg.host) cfg.host=argv[++i];
        else if (!strcmp(argv[i],"--port") && i+1<argc) cfg.port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--speed-only")) speed_only=1;
        else if (!strcmp(argv[i],"--n57-sgbd") && i+1<argc) {
            n57_index=n57_parse_index(argv[++i]);
            if (!n57_index) return 2;
        }
        else if (!strcmp(argv[i],"--udp-port") && i+1<argc) udp_port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--cycles") && i+1<argc) cycles=number(argv[++i],1,1000);
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"auto")) ++i;
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"hsfz")) { cfg.transport=DIAG_HSFZ; ++i; }
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"most")) { cfg.transport=DIAG_MOST; ++i; }
        else return 2;
    }
    if (n57_index) cfg.most_after_hsfz=1;
    else if (native_android_auto_installed()) cfg.transport=DIAG_HSFZ;
    if (speed_only) cfg.dsi_name="launchdeck";
    source=gauge_source_port(cfg.dsi_name);
    if ((!cfg.host && cfg.transport==DIAG_HSFZ) || parent<=1) return 2;
    diag_link_signals();
    diag_link_init(&link,&cfg);
    active=gauge_bind(source);
    if (active<0) return 1;
    sock=socket(AF_INET,SOCK_DGRAM,0);
    if (sock<0) { close(active); return 1; }
    memset(&dest,0,sizeof dest);
    dest.sin_family=AF_INET;
    dest.sin_port=htons((uint16_t)udp_port);
    inet_pton(AF_INET,"127.0.0.1",&dest.sin_addr);
    while (diag_running && getppid()==parent && (!cycles || count<cycles) && !diag_link_lost(&link)) {
        struct drive_frame f;
        struct hud_telemetry telemetry={HUD_TELEMETRY_MAGIC,0,0,0,0,0,0};
        int rpm=-1,kph=-1,rc,publish_telemetry=1;
        uint64_t start=bd_mono_ms(), elapsed;
        telemetry.source=source;
        telemetry.valid=n57_index ? HUD_SAMPLE_N57 : 0;
        int up=diag_link_up(&link);
        if (up) {
            if (n57_index) {
                struct drive_n57_frame native={DRIVE_N57_MAGIC,0,-1,-1};
                struct drive_n57_exchange sample={&link,0};
                double value, speed;
                native.sampled_ms=start;
                if (!diag_link_begin(&link)) {
                    /* Lease contention is no new measurement; the previous sample still ages normally. */
                    publish_telemetry=0;
                } else {
                    rc=n57_match(n57_link_exchange,&sample,n57_index);
                    if (rc==1) {
                        if (speed_only) {
                            rc=n57_read(n57_link_exchange,&sample,0x4c14,&value);
                            if (rc==1) native.mkph=(int32_t)round(value*1000);
                        } else {
                            rc=n57_read_drive(n57_link_exchange,&sample,&value,&speed);
                            if (rc==1) {
                                native.rpm=(int32_t)round(value);
                                native.mkph=(int32_t)round(speed*1000);
                            }
                        }
                    }
                    if (sample.sampled_ms) native.sampled_ms=sample.sampled_ms;
                    diag_link_end(&link,rc==1);
                    if (rc<0) diag_link_drop(&link);
                    if (native.rpm>=0) {
                        telemetry.rpm=(unsigned)native.rpm;
                        telemetry.rpm_ms=native.sampled_ms;
                        telemetry.valid|=HUD_SAMPLE_RPM;
                    }
                    if (sendto(sock,&native,sizeof native,0,(struct sockaddr *)&dest,sizeof dest)!=(ssize_t)sizeof native) break;
                    diag_link_should_yield(&link,bd_mono_ms());
                }
            } else {
                if (speed_only) {
                    rc=read_live_pid(&link,0x0d,1,&kph);
                } else {
                    rc=read_live_pid(&link,0x0c,2,&rpm);
                    if (!rc && start>=speed_due) {
                        rc=read_live_pid(&link,0x0d,1,&last_speed);
                        speed_due=start+65;
                    }
                    kph=last_speed;
                }
                if (rc==-1) diag_link_drop(&link);
                else if (!rc) {
                    f.sampled_ms=bd_mono_ms();
                    f.rpm=rpm;
                    f.kph=kph;
                    if (rpm>=0 && rpm<=8000) {
                        telemetry.rpm=(unsigned)rpm;
                        telemetry.rpm_ms=f.sampled_ms;
                        telemetry.valid|=HUD_SAMPLE_RPM;
                    }
                    if (sendto(sock,&f,sizeof f,0,(struct sockaddr *)&dest,sizeof dest)!=(ssize_t)sizeof f) break;
                    diag_link_should_yield(&link,f.sampled_ms);
                }
            }
        }
        if (publish_telemetry) gauge_publish(active,&telemetry);
        diag_link_tick(&link);
        ++count;
        elapsed=bd_mono_ms()-start;
        if (n57_index) {
            if (diag_running) poll(0,0,elapsed<100 ? (int)(100-elapsed) : 5);
            continue;
        }
        if (diag_running) poll(0,0,!up ? 25 : elapsed<20 ? (int)(20-elapsed) : 2);
    }
    lost=diag_running && diag_link_lost(&link);
    diag_link_close(&link);
    close(sock);
    close(active);
    return lost ? 1 : 0;
}
