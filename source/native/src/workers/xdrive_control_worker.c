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


/* HSFZ first, MOST after a failed connect (cfg.transport in main). A probe on one car got `22 F1 50` replies from DSC and EGS over MOST and a refusal for the transfer case (MOST.md); no control request was tried. */
static void stop(int sig) { (void)sig; diag_running = 0; }
static int exchange(struct diag_link *link, unsigned ecu, const unsigned char *uds, unsigned uds_size,
                    const unsigned char *prefix, unsigned prefix_size,
                    unsigned char *reply, unsigned reply_size, unsigned timeout_ms,
                    unsigned *negative) {
    return diag_link_request(link, ecu, uds, uds_size, prefix, prefix_size, reply, reply_size,
                             timeout_ms, negative);
}
#include <ctype.h>
#include "dsc_control_table.h"
static int read_mode(struct diag_link *fd) {
    const unsigned char req[]={0x22,0xdb,0x32}, ack[]={0x62,0xdb,0x32};
    unsigned char reply[64];
    unsigned negative;
    const int n=exchange(fd,0x19,req,sizeof req,ack,sizeof ack,reply,sizeof reply,800,&negative);
    if (n<11) return -1;
    if ((reply[10]&0xc0)==0xc0) return 0;
    if (reply[10]==0x20 || reply[10]==0x21) return 1;
    return -1;
}

// F-series LMV 0F1480 (lmv_01.prg STATUS_SOLLMOMENT, 0x4002): byte 3 is the target qualifier.
// 0x20 applies the DSC target (AWD); 0x28 opens the clutch and 0x70 is rear-wheel drive.
// With the engine off it reports 0x60 (fallback), so the mode is only readable while running.
static unsigned read_sgbd(struct diag_link *fd, unsigned ecu, int *length, unsigned *negative) {
    const unsigned char req[]={0x22,0xf1,0x50}, prefix[]={0x62,0xf1,0x50};
    unsigned char reply[16];
    int n=exchange(fd,ecu,req,sizeof req,prefix,sizeof prefix,reply,sizeof reply,800,negative);
    if (length) *length=n;
    if (n<6 || n>10) return 0;
    for (int i=3;i<n-3;++i) if (reply[i]) return 0;
    return (unsigned)reply[n-3]<<16 | (unsigned)reply[n-2]<<8 | reply[n-1];
}

static int lmv_mode(struct diag_link *fd) {
    const unsigned char req[]={0x22,0x40,0x02}, ack[]={0x62,0x40,0x02};
    unsigned char reply[64];
    unsigned negative;
    if (exchange(fd,0x19,req,3,ack,3,reply,sizeof reply,800,&negative)<6) return -1;
    if (reply[5]==0x20) return 1;
    if (reply[5]==0x28 || reply[5]==0x70) return 0;
    return -1;
}

static int parked(struct diag_link *fd, int running) {
    const unsigned char speed[]={1,0x0d}, speed_ack[]={0x41,0x0d};
    const unsigned char gear[]={0x22,0xda,0x2e}, gear_ack[]={0x62,0xda,0x2e};
    const unsigned char rpm[]={1,0x0c}, rpm_ack[]={0x41,0x0c};
    unsigned char reply[8];
    unsigned negative;
    // EGS DA2E byte 2 is the Park state: 06 in Park with the engine off, 01 in Park with it running
    // (DSC_25 car, 0a in R/N/D). Missing or different replies block the write.
    return exchange(fd,0x18,gear,3,gear_ack,3,reply,sizeof reply,350,&negative)==5 && reply[3]==0 &&
           reply[4]==(running ? 1 : 6) &&
           exchange(fd,0x12,rpm,2,rpm_ack,2,reply,sizeof reply,350,&negative)==4 && ((reply[2]|reply[3])!=0)==running &&
           exchange(fd,0x12,speed,2,speed_ack,2,reply,sizeof reply,350,&negative)==3 && !reply[2];
}

static void control(struct diag_link *fd, const char *mode, unsigned value) {
    const unsigned char capability[]={0x22,0xdb,0xe1}, cap_ack[]={0x62,0xdb,0xe1};
    unsigned char reply[64];
    unsigned negative;
    int n, current, fseries;
    const unsigned index=read_sgbd(fd,0x29,&n,&negative);
    const struct dsc_control_row *row=dsc_control_row(index);
    if (n == -2) { printf("DSC identification refused (NRC %02X).\n",negative); return; }
    if (n < 0) { puts("No DSC reply at 0x29. Check the diagnostic gateway."); return; }
    if (!index) { puts("DSC replied with an unrecognized identification format."); return; }
    if (!row || !row->verified_write) {
        if (!strcmp(mode,"--status")) {
            const unsigned lmv=read_sgbd(fd,0x19,0,&negative);
            const unsigned vip=read_sgbd(fd,0x39,0,&negative);
            char lmv_id[12]="unknown", vip_id[12]="unknown";
            if (lmv) snprintf(lmv_id,sizeof lmv_id,"%06X",lmv);
            if (vip) snprintf(vip_id,sizeof vip_id,"%06X",vip);
            if (row) printf("%s (%s, %06X) is identified but not yet verified for control. Transfer case %s. VIP %s.\n",
                            row->sgbd,row->chassis,index,lmv_id,vip_id);
            else printf("Unsupported DSC %06X. Transfer case %s. VIP %s.\n",index,lmv_id,vip_id);
        } else if (row) printf("%s (%s, %06X) is identified but not yet verified for control.\n",row->sgbd,row->chassis,index);
        else printf("DSC %06X is outside the supported control list.\n",index);
        return;
    }
    n=exchange(fd,0x29,capability,3,cap_ack,3,reply,sizeof reply,800,&negative);
    if (n<4) {
        if (n==-2) printf("DSC mode read refused (NRC %02X).\n",negative);
        else puts("DSC mode read unavailable.");
        return;
    }
    // DSC_25 (F-series) drops LMV control with the engine off, so it switches with the engine running.
    fseries=index==0x0f1860;
    if (fseries && read_sgbd(fd,0x19,0,&negative)!=0x0f1480) {
        puts("DSC_25 requires a verified LMV 0F1480 transfer case.");
        return;
    }
    current=fseries ? lmv_mode(fd) : read_mode(fd);
    if (!strcmp(mode,"--status")) {
        puts(current<0 ? (fseries?"Start the engine to read the transfer case.":"Transfer case mode unavailable.") :
             current?"Transfer case reports AWD.":"Transfer case reports RWD.");
        return;
    }
    // dsc_25.prg documents DBE1 as writable with 1 = active; restore verified on a 0F1860 car.
    if (value && index!=0x0f2850 && !fseries) { puts("Cycle the ignition to restore AWD on this DSC."); return; }
    if (current<0) { puts(fseries?"Start the engine to read the transfer case.":"Transfer case mode unavailable."); return; }
    if (!parked(fd,fseries)) {
        puts(fseries?"Needs engine running, stationary, and confirmed Park.":"Needs ignition on, engine off, stationary, and confirmed Park.");
        return;
    }
    {
        const unsigned char request[]={0x2e,0xdb,0xe1,(unsigned char)value}, ack[]={0x6e,0xdb,0xe1};
        const struct timespec pause={0,250000000};
        n=exchange(fd,0x29,request,4,ack,3,reply,sizeof reply,2400,&negative);
        if (n==-2) { printf("DSC refused this request (NRC %02X).\n",negative); return; }
        // DSC_25 acknowledged after more than 2 s on the car, so read back even without an ack.
        const uint64_t deadline=bd_mono_ms()+2000;
        while (bd_mono_ms()<deadline) {
            current=fseries ? lmv_mode(fd) : read_mode(fd);
            if (current==(int)value) break;
            nanosleep(&pause,0);
        }
        if (current==(int)value) puts(value?"Transfer case confirms AWD.":"Transfer case confirms RWD.");
        else puts(n>=3?"DSC accepted. Transfer case mode is unconfirmed.":"DSC request could not be confirmed.");
    }
}

int main(int argc,char **argv) {
    unsigned value, port=6801;
    struct diag_link link;
    struct diag_link_cfg cfg;
    if ((argc!=5 && argc!=7) || strcmp(argv[1],"--host")) return 2;
    if (argc==7) {
        if (strcmp(argv[5],"--port")) return 2;
        port=number(argv[6],1,65535);
    }
    if (strcmp(argv[3],"--status") && strcmp(argv[3],"--drive-mode")) return 2;
    value=number(argv[4],0,!strcmp(argv[3],"--map") ? 4 : !strcmp(argv[3],"--flap") ? 2 : !strcmp(argv[3],"--burbles") ? 3 : 1);
    if (!strcmp(argv[3],"--map") && !value) return 2;
    signal(SIGINT,stop); signal(SIGTERM,stop); signal(SIGPIPE,SIG_IGN);
    memset(&cfg,0,sizeof cfg);
    cfg.host=argv[2]; cfg.port=port; cfg.transport=DIAG_AUTO; cfg.kind=DIAG_ONESHOT; cfg.dsi_name="xdrivectl";
    diag_link_init(&link,&cfg);
    if (!diag_link_reserve_oneshot(&link)) { puts("Diagnostics busy. Try again."); return 0; }
    if (diag_link_up(&link)) control(&link,argv[3],value);
    else puts("Diagnostic gateway unavailable.");
    diag_link_close(&link);
    return 0;
}
