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



/* The shared MOST tunnel cannot address DSC, EGS or the transfer case. */
static void stop(int sig) { (void)sig; diag_running = 0; }
static int connect_gw(const char *host, unsigned port) { return diag_hsfz_connect(host, port); }
static int exchange(int fd, unsigned ecu, const unsigned char *uds, unsigned uds_size,
                    const unsigned char *prefix, unsigned prefix_size,
                    unsigned char *reply, unsigned reply_size, unsigned timeout_ms,
                    unsigned *negative) {
    return diag_hsfz_exchange(fd, ecu, uds, uds_size, prefix, prefix_size, reply, reply_size,
                              timeout_ms, negative);
}
#include <ctype.h>
/* Included by collector.c for Bootmod3 and xDrive controls. */
#include <sys/stat.h>
#ifndef ID6_XDRIVE_OWNER
#define ID6_XDRIVE_OWNER "/var/opt/hmi/data/ID5/xdrive-owner-vin.txt"
#endif
static unsigned n57_index;
static uint64_t parked_at;
enum { N57_PARK_SNAPSHOT_MS=750 };

static int n57_fd_exchange(void *ctx, unsigned ecu, const unsigned char *req, unsigned n,
                            const unsigned char *prefix, unsigned pn, unsigned char *reply,
                            unsigned cap, unsigned timeout, unsigned *negative) {
    return exchange(*(int *)ctx,ecu,req,n,prefix,pn,reply,cap,timeout,negative);
}

static int owner_consent(int fd) {
    const char *want=getenv("NBTEVO_OWN_VIN");
    char saved[18], extra;
    unsigned char reply[32];
    const unsigned char req[]={0x22,0xf1,0x90}, ack[]={0x62,0xf1,0x90};
    unsigned negative, i;
    int n, start=3;
    if (!want) {
        struct stat st;
        FILE *file;
        if (lstat(ID6_XDRIVE_OWNER,&st) || !S_ISREG(st.st_mode) || st.st_uid!=geteuid() || (st.st_mode & 0777)!=0600)
            return 0;
        file=fopen(ID6_XDRIVE_OWNER,"r");
        if (!file) return 0;
        n=fscanf(file,"%17s %c",saved,&extra);
        fclose(file);
        if (n!=1) return 0;
        want=saved;
    }
    if (strlen(want)!=17) return 0;
    for (i=0;i<17;i++)
        if (!((want[i]>='A' && want[i]<='Z') || (want[i]>='0' && want[i]<='9')) ||
            want[i]=='I' || want[i]=='O' || want[i]=='Q') return 0;
    n=exchange(fd,0x12,req,3,ack,3,reply,sizeof reply,800,&negative);
    if (n<20) return 0;
    while (n>start && (reply[n-1]==0 || reply[n-1]==' ')) n--;
    while (start<n && (reply[start]==0 || reply[start]==' ')) start++;
    return n-start==17 && !memcmp(reply+start,want,17);
}

static int read_mode(int fd) {
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
static unsigned read_sgbd(int fd, unsigned ecu, int *length, unsigned *negative) {
    const unsigned char req[]={0x22,0xf1,0x50}, prefix[]={0x62,0xf1,0x50};
    unsigned char reply[16];
    int n=exchange(fd,ecu,req,sizeof req,prefix,sizeof prefix,reply,sizeof reply,800,negative);
    if (length) *length=n;
    if (n<6 || n>10) return 0;
    for (int i=3;i<n-3;++i) if (reply[i]) return 0;
    return (unsigned)reply[n-3]<<16 | (unsigned)reply[n-2]<<8 | reply[n-1];
}

static int lmv_mode(int fd) {
    const unsigned char req[]={0x22,0x40,0x02}, ack[]={0x62,0x40,0x02};
    unsigned char reply[64];
    unsigned negative;
    if (exchange(fd,0x19,req,3,ack,3,reply,sizeof reply,800,&negative)<6) return -1;
    if (reply[5]==0x20) return 1;
    if (reply[5]==0x28 || reply[5]==0x70) return 0;
    return -1;
}

static int parked(int fd, int running) {
    const unsigned char speed[]={1,0x0d}, speed_ack[]={0x41,0x0d};
    const unsigned char gear[]={0x22,0xda,0x2e}, gear_ack[]={0x62,0xda,0x2e};
    const unsigned char rpm[]={1,0x0c}, rpm_ack[]={0x41,0x0c};
    unsigned char reply[8];
    unsigned negative;
    if (running && n57_index) {
        double speed, rpm_value;
        parked_at=bd_mono_ms();
        return n57_park(n57_fd_exchange,&fd)==1 &&
               n57_match(n57_fd_exchange,&fd,n57_index)==1 &&
               n57_read_drive(n57_fd_exchange,&fd,&rpm_value,&speed)==1 &&
               rpm_value>0 && speed==0 && bd_mono_ms()-parked_at<=N57_PARK_SNAPSHOT_MS;
    }
    // Preserve the separate upstream reference profile; never use its gear read for pinned N57.
    if (!(exchange(fd,0x18,gear,3,gear_ack,3,reply,sizeof reply,350,&negative)==5 && reply[3]==0 && reply[4]==(running ? 1 : 6))) return 0;
    return
           exchange(fd,0x12,rpm,2,rpm_ack,2,reply,sizeof reply,350,&negative)==4 && ((reply[2]|reply[3])!=0)==running &&
           exchange(fd,0x12,speed,2,speed_ack,2,reply,sizeof reply,350,&negative)==3 && !reply[2];
}

static void control(int fd, const char *mode, unsigned value) {
    const unsigned char capability[]={0x22,0xdb,0xe1}, cap_ack[]={0x62,0xdb,0xe1};
    unsigned char reply[64];
    unsigned negative;
    int n, current, fseries;
    const unsigned index=read_sgbd(fd,0x29,&n,&negative);
    if (n == -2) { printf("DSC identification refused (NRC %02X).\n",negative); return; }
    if (n < 0) { puts("No DSC reply at 0x29. Check the diagnostic gateway."); return; }
    if (!index) { puts("DSC replied with an unrecognized identification format."); return; }
    if (index!=0x0f2510 && index!=0x0f2850 && index!=0x0f1860) {
        if (!strcmp(mode,"--status")) {
            const unsigned lmv=read_sgbd(fd,0x19,0,&negative);
            const unsigned vip=read_sgbd(fd,0x39,0,&negative);
            char lmv_id[12]="unknown", vip_id[12]="unknown";
            if (lmv) snprintf(lmv_id,sizeof lmv_id,"%06X",lmv);
            if (vip) snprintf(vip_id,sizeof vip_id,"%06X",vip);
            printf("Unsupported DSC %06X. Transfer case %s. VIP %s.\n",index,lmv_id,vip_id);
        } else printf("DSC %06X is outside the supported control list.\n",index);
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
    if (n57_index && !fseries) {
        puts("N57 mode changes require DSC_25 0F1860 with LMV 0F1480. No mode change sent.");
        return;
    }
    // dsc_25.prg documents DBE1 as writable with 1 = active; restore verified on a 0F1860 car.
    if (value && index!=0x0f2850 && !fseries) { puts("Cycle the ignition to restore AWD on this DSC."); return; }
    if (current<0) { puts(fseries?"Start the engine to read the transfer case.":"Transfer case mode unavailable."); return; }
    if (!owner_consent(fd)) { puts("Owner VIN missing, unreadable or mismatched. No mode change sent."); return; }
    if (!parked(fd,fseries)) {
        puts(fseries?"Needs engine running, stationary, and confirmed Park.":"Needs ignition on, engine off, stationary, and confirmed Park.");
        return;
    }
    {
        const unsigned char request[]={0x2e,0xdb,0xe1,(unsigned char)value}, ack[]={0x6e,0xdb,0xe1};
        const struct timespec pause={0,250000000};
        if (n57_index && bd_mono_ms()-parked_at>N57_PARK_SNAPSHOT_MS) {
            puts("Park/drive snapshot expired. No mode change sent."); return;
        }
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
    int fd,lock;
    if (argc<5 || argc%2!=1 || strcmp(argv[1],"--host")) return 2;
    n57_index=n57_index_file(ID6_N57_SETTINGS);
    if (!n57_index && access(ID6_N57_SETTINGS,F_OK)==0) {
        puts("Invalid N57 DDE identity setting; no mode change sent."); return 2;
    }
    for (int i=5;i<argc;i+=2) {
        if (!strcmp(argv[i],"--port")) port=number(argv[i+1],1,65535);
        else if (!strcmp(argv[i],"--n57-sgbd")) {
            n57_index=n57_parse_index(argv[i+1]);
            if (!n57_index) return 2;
        }
        else return 2;
    }
    if (strcmp(argv[3],"--status") && strcmp(argv[3],"--drive-mode")) return 2;
    value=number(argv[4],0,!strcmp(argv[3],"--map") ? 4 : !strcmp(argv[3],"--flap") ? 2 : !strcmp(argv[3],"--burbles") ? 3 : 1);
    if (!strcmp(argv[3],"--map") && !value) return 2;
    signal(SIGINT,stop); signal(SIGTERM,stop); signal(SIGPIPE,SIG_IGN);
    lock=reserve_gateway();
    if (lock<0) { puts("Diagnostics busy. Try again."); return 0; }
    fd=connect_gw(argv[2],port);
    if (fd>=0) { control(fd,argv[3],value); close(fd); }
    else puts("Diagnostic gateway unavailable.");
    close(lock);
    return 0;
}
