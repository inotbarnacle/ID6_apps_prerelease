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


typedef struct diag_link *control_link;
static int exchange(control_link link, unsigned ecu, const unsigned char *uds, unsigned uds_size,
                    const unsigned char *prefix, unsigned prefix_size,
                    unsigned char *reply, unsigned reply_size, unsigned timeout_ms,
                    unsigned *negative) {
    return diag_link_request(link, ecu, uds, uds_size, prefix, prefix_size, reply, reply_size,
                             timeout_ms, negative);
}
#include <ctype.h>
#include "bm3_profiles.h"

enum { BM3_MARKER_SIZE = 255 };

/* Binary markers captured upstream on 64/80-byte MG1 layouts and the G87 S58. */
static int bm3_match(const unsigned char *marker) {
    unsigned i, end, hash=0, hashes=0, binary=0, s58=0;
    int matched=-1;
    if (!memcmp(marker,"BM3_",4)) {
        static const unsigned delimiters[]={68,84};
        for (i=0;i<sizeof delimiters/sizeof delimiters[0];++i) {
            unsigned p=delimiters[i];
            if (memcmp(marker+p,"~|~_",4)) continue;
            p+=4;
            while (p<BM3_MARKER_SIZE && marker[p]=='_') ++p;
            if (p<BM3_MARKER_SIZE && marker[p]=='#') {
                if (binary) return -1;
                hash=p;
                binary=1;
            }
        }
        if (!binary) {
            for (i=0;i<BM3_MARKER_SIZE;++i) if (marker[i]=='#') { hash=i; ++hashes; }
            s58=hashes==1 && hash==101 && marker[hash+15]=='_';
        }
    }
    if (binary || s58) {
        for (end=hash+1;end<BM3_MARKER_SIZE && marker[end];++end) {
            unsigned c=marker[end];
            if (!(c>='A' && c<='Z') && !(c>='0' && c<='9') && !(s58 && c=='_')) return -1;
        }
        if (end==BM3_MARKER_SIZE || (s58 && end<hash+16)) return -1;
        if (s58) {
            for (i=hash+1;i<hash+15;++i) if (marker[i]=='_') return -1;
        }
        for (i=0;i<sizeof profiles/sizeof profiles[0];++i) {
            size_t size=strlen(profiles[i].rom);
            if (s58 ? size!=9 : end-hash-1!=size+5) continue;
            if (memcmp(marker+hash+1,profiles[i].rom,size)) continue;
            if (matched>=0) return -1;
            matched=(int)i;
        }
        return matched;
    }
    for (i=0;i+5<=BM3_MARKER_SIZE;++i) if (!memcmp(marker+i,"BM3_R",5)) break;
    if (i+5>BM3_MARKER_SIZE) return -1;
    for (i=0;i<sizeof profiles/sizeof profiles[0];++i) {
        const size_t size=strlen(profiles[i].rom);
        for (unsigned j=0;j+size+1<=BM3_MARKER_SIZE;++j) {
            if (marker[j]!='#' || memcmp(marker+j+1,profiles[i].rom,size) ||
                (j+size+1<BM3_MARKER_SIZE && isalnum(marker[j+size+1]))) continue;
            if (matched>=0 && matched!=(int)i) return -1;
            matched=(int)i;
        }
    }
    return matched;
}

/* These locations are shared with EntryNav Bootmod3 Suite for automatic ROM lookup. */
static int unsupported_marker_read(unsigned nrc) {
    return nrc==0x11 || nrc==0x12 || nrc==0x13 || nrc==0x31 || nrc==0x7e || nrc==0x7f;
}

static int bm3_profile(control_link link) {
    static const unsigned char addresses[][4]={
        {0x80,0x7f,0xfe,0x50},{0x09,0x77,0xfe,0x4c},
        {0x80,0x1f,0xfd,0xac},{0x80,0x05,0xfe,0x1e},
    };
    static const unsigned char clear[]={0x2c,3,0xf3,1}, cleared[]={0x6c,3,0xf3,1};
    static const unsigned char defined[]={0x6c,2,0xf3,1}, read[]={0x22,0xf3,1}, prefix[]={0x62,0xf3,1};
    static const unsigned char direct_ack[]={0x63};
    unsigned char reply[512];
    for (unsigned i=0;i<sizeof addresses/sizeof addresses[0] && diag_running;++i) {
        unsigned char define[]={0x2c,2,0xf3,1,0x24,0,0,0,0,0,BM3_MARKER_SIZE};
        unsigned char direct[]={0x23,0x14,0,0,0,0,BM3_MARKER_SIZE};
        unsigned negative, offset=3;
        memcpy(define+5,addresses[i],4);
        memcpy(direct+2,addresses[i],4);
        // Defining F301 again appends data unless the previous range is cleared.
        int n=exchange(link,0x12,clear,sizeof clear,cleared,sizeof cleared,reply,sizeof reply,1600,&negative);
        if (n>=4) n=exchange(link,0x12,define,sizeof define,defined,sizeof defined,reply,sizeof reply,1600,&negative);
        if (n>=4) n=exchange(link,0x12,read,sizeof read,prefix,sizeof prefix,reply,sizeof reply,1600,&negative);
        if (n==-2 && unsupported_marker_read(negative)) {
            offset=1;
            n=exchange(link,0x12,direct,sizeof direct,direct_ack,sizeof direct_ack,reply,sizeof reply,1600,&negative);
        }
        if (n==-2 && unsupported_marker_read(negative)) continue;
        if (n<0) {
            if (n==-2) printf("CustomROM identification refused (NRC %02X).\n",negative);
            else puts("No Bootmod3 CustomROM identification reply.");
            return -1;
        }
        if (n==(int)(offset+BM3_MARKER_SIZE)) {
            const int matched=bm3_match(reply+offset);
            if (matched>=0) return matched;
        }
    }
    puts("This Bootmod3 CustomROM is outside the supported profile list.");
    return -1;
}

static int write_memory(control_link fd, unsigned char sid, const unsigned char *address,
                       const unsigned char *bytes, unsigned size) {
    unsigned char req[31]={sid,0x14,address[0],address[1],address[2],address[3],(unsigned char)size};
    const unsigned char ack[]={ (unsigned char)(sid+0x40),0x3d };
    unsigned char reply[64];
    unsigned negative;
    memcpy(req+7,bytes,size);
    return exchange(fd,0x12,req,7+size,ack,sid==0x3e?2:1,reply,sizeof reply,1600,&negative)>0;
}

static int verify_memory(control_link fd, const unsigned char *address, const unsigned char *bytes, unsigned size) {
    const unsigned char req[]={0x23,0x14,address[0],address[1],address[2],address[3],(unsigned char)size};
    const unsigned char ack[]={0x63};
    unsigned char reply[32];
    unsigned negative;
    return exchange(fd,0x12,req,sizeof req,ack,sizeof ack,reply,sizeof reply,1600,&negative)==(int)size+1
           && !memcmp(reply+1,bytes,size);
}

/* Return the displayed slot (1–4), or zero when the ECU cannot report it. */
static unsigned current_map(control_link fd, const unsigned char *address, unsigned *negative) {
    const unsigned char req[]={0x23,0x14,address[0],address[1],address[2],address[3],1};
    const unsigned char ack[]={0x63};
    unsigned char reply[8];
    *negative=0;
    int n=exchange(fd,0x12,req,sizeof req,ack,sizeof ack,reply,sizeof reply,1600,negative);
    return n==2 && reply[1]<=3 ? (unsigned)reply[1]+1 : 0;
}

static void control(control_link fd, const char *mode, unsigned value) {
    unsigned char reply[512];
    unsigned negative;
    int n;
    // These two requests have no ROM-dependent addresses.
    if (!strcmp(mode,"--flap")) {
        const unsigned char req[]={0x2f,0x60,0xfd,value?3:0,value==1?0x14:0x23,value==1?0xfe:0x28};
        const unsigned char ack[]={0x6f,0x60,0xfd,req[3]};
        n=exchange(fd,0x12,req,value?6:4,ack,sizeof ack,reply,sizeof reply,1600,&negative);
        /* S55 (MEVD17.2.G) rejects the 6-byte override. Upstream issue #42 captured these
           8-byte forms being acknowledged on an F80 M3; valve position was not verified. */
        if (value && n==-2 && (negative==0x13 || negative==0x31)) {
            const unsigned char wide[]={0x2f,0x60,0xfd,3,value==1?0x14:0xeb,value==1?0x7a:0x85,0xfa,0xff};
            n=exchange(fd,0x12,wide,sizeof wide,ack,sizeof ack,reply,sizeof reply,1600,&negative);
        }
        if (n>=4) puts("Exhaust flap request accepted by DME.");
        else puts("Exhaust flap request could not be confirmed.");
        return;
    }
    if (!strcmp(mode,"--sound")) {
        const unsigned char req[]={0x2e,0x5f,0xfd,value?0:1}, ack[]={0x6e,0x5f,0xfd};
        n=exchange(fd,0x12,req,sizeof req,ack,sizeof ack,reply,sizeof reply,1600,&negative);
        if (n>=3) puts("Active Sound request accepted by DME.");
        else puts("Active Sound request could not be confirmed.");
        return;
    }
    const int profile=bm3_profile(fd);
    if (profile<0) return;
    if (!strcmp(mode,"--current-map")) {
        unsigned actual=current_map(fd,profiles[profile].map,&negative);
        if (actual) printf("Current map: %u\n",actual);
        else if (negative) printf("Current map read refused (NRC %02X).\n",negative);
        else puts("Current map unavailable.");
    } else if (!strcmp(mode,"--map") && value>=1 && value<=4) {
        const unsigned char *address=profiles[profile].map;
        const unsigned char command[]={ (unsigned char)(value-1),1 };
        int accepted=write_memory(fd,profiles[profile].sid,address,command,sizeof command);
        unsigned actual=current_map(fd,address,&negative);
        if (accepted && actual==value) printf("Map %u verified by DME readback.\n",actual);
        else if (actual) printf("Map %u change unconfirmed. Current map: %u.\n",value,actual);
        else puts("Map change unconfirmed. Current map unavailable.");
    } else if (!strcmp(mode,"--burbles") && value<=3) {
        const unsigned char sid=profiles[profile].sid;
        const unsigned char *enable=profiles[profile].enable, *timing=profiles[profile].timing;
        const unsigned factor=profiles[profile].factor;
        const unsigned char enabled=value!=0;
        unsigned char table[24];
        if ((!enable[0] && !enable[1] && !enable[2] && !enable[3]) ||
            (!timing[0] && !timing[1] && !timing[2] && !timing[3]) || !factor || 12*factor>127) {
            puts("Burble preset unavailable for this ROM.");
            return;
        }
        memset(table,(unsigned char)(0u-value*4*factor),sizeof table);
        if ((!value && !write_memory(fd,sid,enable,&enabled,1)) ||
            !write_memory(fd,sid,timing,table,sizeof table) ||
            (value && !write_memory(fd,sid,enable,&enabled,1)) ||
            !verify_memory(fd,timing,table,sizeof table) || !verify_memory(fd,enable,&enabled,1)) {
            puts("Burble request incomplete or unconfirmed.");
            return;
        }
        if (value) printf("Burble preset %u verified by DME readback.\n",value*4);
        else puts("Burbles off, verified by DME readback.");
    }
}

int main(int argc,char **argv) {
    unsigned value, port=6801;
    if (argc<5 || strcmp(argv[1],"--host")) return 2;
    for (int i=5;i<argc;i+=2) {
        if (i+1>=argc) return 2;
        if (!strcmp(argv[i],"--port")) port=number(argv[i+1],1,65535);
        else return 2;
    }
    if (strcmp(argv[3],"--current-map") && strcmp(argv[3],"--map") && strcmp(argv[3],"--flap") && strcmp(argv[3],"--sound") && strcmp(argv[3],"--burbles")) return 2;
    value=number(argv[4],0,!strcmp(argv[3],"--map") ? 4 : !strcmp(argv[3],"--flap") ? 2 : !strcmp(argv[3],"--burbles") ? 3 : 1);
    if (!strcmp(argv[3],"--map") && !value) return 2;
    struct diag_link link;
    struct diag_link_cfg cfg;
    const int burbles = !strcmp(argv[3], "--burbles");
    diag_link_signals();
    memset(&cfg, 0, sizeof cfg);
    cfg.host = argv[2];
    cfg.port = port;
    cfg.transport = burbles ? DIAG_HSFZ : DIAG_AUTO;
    cfg.kind = DIAG_ONESHOT;
    cfg.dsi_name = "bootmod3";
    diag_link_init(&link, &cfg);
    if (!diag_link_reserve_oneshot(&link)) {
        puts("Diagnostics busy. Try again.");
        return 0;
    }
    if (diag_link_up(&link)) control(&link, argv[3], value);
    else if (burbles) puts("Burble presets require the HSFZ gateway.");
    else puts("Diagnostic gateway unavailable.");
    diag_link_close(&link);
    return 0;
}
