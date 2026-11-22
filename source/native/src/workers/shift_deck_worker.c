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


#include "drive_frame.h"

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
    struct diag_link_cfg cfg={0,6801,DIAG_AUTO,DIAG_LOOP,"shiftdeck",1};
    struct diag_link link;
    unsigned udp_port=DRIVE_PORT, cycles=0, count=0;
    pid_t parent=getppid();
    struct sockaddr_in dest;
    int sock, active, i, lost, speed_only=0;
    uint64_t speed_due=0;
    int last_speed=-1;
    for (i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && i+1<argc && !cfg.host) cfg.host=argv[++i];
        else if (!strcmp(argv[i],"--port") && i+1<argc) cfg.port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--speed-only")) speed_only=1;
        else if (!strcmp(argv[i],"--udp-port") && i+1<argc) udp_port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--cycles") && i+1<argc) cycles=number(argv[++i],1,1000);
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"auto")) ++i;
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"hsfz")) { cfg.transport=DIAG_HSFZ; ++i; }
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"most")) { cfg.transport=DIAG_MOST; ++i; }
        else return 2;
    }
    if ((!cfg.host && cfg.transport==DIAG_HSFZ) || parent<=1) return 2;
    if (speed_only) cfg.dsi_name = "launchdeck";
    diag_link_signals();
    diag_link_init(&link,&cfg);
    const unsigned source = speed_only ? LAUNCH_ACTIVE_PORT : SHIFT_ACTIVE_PORT;
    active = gauge_bind(source);
    if (active < 0) return 1;
    sock=socket(AF_INET,SOCK_DGRAM,0);
    if (sock<0) { close(active); return 1; }
    memset(&dest,0,sizeof dest);
    dest.sin_family=AF_INET;
    dest.sin_port=htons((uint16_t)udp_port);
    inet_pton(AF_INET,"127.0.0.1",&dest.sin_addr);
    while (diag_running && getppid()==parent && (!cycles || count<cycles) && !diag_link_lost(&link)) {
        struct drive_frame f;
        int rpm=-1,kph=-1,rc;
        uint64_t start=bd_mono_ms(), elapsed;
        int up=diag_link_up(&link);
        if (up) {
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
                if (!speed_only) {
                    struct hud_telemetry sample = { f.sampled_ms, source,
                        rpm >= 0 ? HUD_SAMPLE_RPM : 0, rpm >= 0 ? (unsigned)rpm : 0, 0 };
                    gauge_publish(active, &sample);
                }
                if (sendto(sock,&f,sizeof f,0,(struct sockaddr *)&dest,sizeof dest)!=(ssize_t)sizeof f) break;
            }
            (void)diag_link_should_yield(&link,bd_mono_ms());
        }
        diag_link_tick(&link);
        ++count;
        elapsed=bd_mono_ms()-start;
        if (diag_running) poll(0,0,!up ? 25 : elapsed<20 ? (int)(20-elapsed) : 10);
    }
    lost=diag_link_lost(&link);
    diag_link_close(&link);
    close(sock);
    close(active);
    return lost ? 1 : 0;
}
