#define _POSIX_C_SOURCE 200809L
#include "gauge_core.h"
#include "diagnostic_lease.h"
#include "gauge_coexistence.h"
#include "diag_link.h"
#include "fault_codes.h"
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

#include <fcntl.h>

/* The result is out: end the plugin's pipe now, before the link's bounded close. */
static void result_done(void) {
    int null;
    fflush(stdout);
    null = open("/dev/null", O_WRONLY);
    if (null >= 0) { dup2(null, 1); close(null); }
}

#include "most/most_link.h"

static void code_wait(unsigned ms) {
    const uint64_t until=bd_mono_ms()+ms;
    uint64_t now;
    while (diag_running && (now=bd_mono_ms())<until) poll(0,0,(int)(until-now));
}

static void restore_code_session(struct diag_link *link) {
    const unsigned char request[]={0x10,0x01}, prefix[]={0x50,0x01};
    unsigned char response[16];
    unsigned negative;
    (void)diag_link_request(link,0x12,request,sizeof request,prefix,sizeof prefix,
                            response,sizeof response,2000,&negative);
}

/* Include active, pending, confirmed and historical failure bits, plus warnings. */
enum { fault_status = FAULT_REQUEST_MASK };

static int read_codes(struct diag_link *link, unsigned char *reply, unsigned capacity,
                      unsigned *negative, int trace) {
    const unsigned char request[]={0x19,0x02,fault_status}, prefix[]={0x59,0x02};
    const int count=diag_link_request(link,0x12,request,sizeof request,prefix,sizeof prefix,
                                      reply,capacity,1800,negative);
    if (trace) {
        if (count<0) {
            if (count==-2) fprintf(stderr,"DME 12 19 02 AF rejected (NRC %02X)\n",*negative);
            else fprintf(stderr,"DME 12 19 02 AF returned no matching reply\n");
        } else {
            fputs("DME 12 19 02 AF reply:",stderr);
            for (int i=0;i<count;++i) fprintf(stderr," %02X",reply[i]);
            fputc('\n',stderr);
        }
    }
    return count;
}

static void fault_codes(struct diag_link *link, int clear, int trace, int status_mask) {
    enum { clear_timeout = 15000 };
    const unsigned char clear_request[]={0x14,0xff,0xff,0xff}, clear_reply[]={0x54};
    unsigned char reply[4094];
    unsigned negative;
    struct fault_reply faults;
    struct fault_record record;
    int n, count=0, shown=0, full, extended=0;
    size_t i;
    if (!diag_link_up(link)) { puts("Diagnostic gateway unavailable"); return; }
    if (clear) {
        n=diag_link_request(link,0x12,clear_request,4,clear_reply,1,reply,sizeof reply,clear_timeout,&negative);
        if (n==-2 && negative==0x21) {
            // The ECU explicitly refused because it was busy. A timeout is never retried.
            code_wait(350);
            n=diag_link_request(link,0x12,clear_request,4,clear_reply,1,reply,sizeof reply,clear_timeout,&negative);
        }
        if (n==-2 && (negative==0x22 || negative==0x31 || negative==0x7e || negative==0x7f)) {
            const unsigned original_nrc=negative;
            const unsigned char request[]={0x10,0x03}, prefix[]={0x50,0x03};
            unsigned session_nrc;
            const int session=diag_link_request(link,0x12,request,2,prefix,2,reply,sizeof reply,2000,&session_nrc);
            if (session>=2) {
                extended=1;
                n=diag_link_request(link,0x12,clear_request,4,clear_reply,1,reply,sizeof reply,clear_timeout,&negative);
            } else if (session==-2 && original_nrc!=0x22) {
                printf("Clear refused (NRC %02X); extended session refused (%02X)\n",original_nrc,session_nrc);
                return;
            } else negative=original_nrc;
        }
        if (n!=1) {
            if (extended) restore_code_session(link);
            if (n==-2 && (negative==0x22 || negative==0x83 || negative==0x88))
                printf("Clear refused (NRC %02X). Park with ignition on and engine off.\n",negative);
            else if (n==-2 && negative==0x33)
                puts("Clear refused (NRC 33). The DME requires authorized diagnostics.");
            else if (n==-2) printf("Clear rejected by DME (NRC %02X)\n",negative);
            else puts("Clear unconfirmed. Read engine codes before retrying.");
            return;
        }
        // The acknowledgement confirms the request. Allow fault memory to settle
        // and verify in the same session before restoring its default.
        code_wait(350);
    }
    n=read_codes(link,reply,sizeof reply,&negative,trace);
    if (clear && n<3 && diag_running) {
        // Recheck is read-only. Reconnect on a lost HSFZ link without issuing another clear.
        if (n==-1 && !diag_link_on_most(link)) diag_link_drop(link);
        if (diag_link_up(link))
            n=read_codes(link,reply,sizeof reply,&negative,trace);
    }
    if (extended) restore_code_session(link);
    if (n==-2) {
        printf(clear ? "Clear accepted; recheck refused (NRC %02X)\n" : "Read rejected (NRC %02X)\n",negative);
        return;
    }
    full = diag_link_on_most(link) && n==MOST_INNER_MAX;   /* the tunnel reply filled its cap */
    if (full) n-=(n-3)%4;   /* it may end mid-record */
    if (n<0 || !fault_parse_reply(reply,(size_t)n,&faults)) {
        puts(clear ? "Clear acknowledged; recheck unavailable" : "No valid code response");
        return;
    }
    if (!(faults.available & fault_status)) {
        puts(clear ? "Clear accepted; requested fault-status flags unavailable" : "Requested fault-status flags unavailable");
        return;
    }
    for (i=0;i<faults.count;++i) {
        if (!fault_record_at(&faults,i,&record)) return;
        if (record.status & record.available & fault_status) ++count;
    }
    if (clear) printf("Clear accepted; %d engine %s remain%s\n",count,
                      count==1 ? "code" : "codes",count==1 ? "s" : "");
    else printf("Engine: %d %s\n",count,count==1 ? "code" : "codes");
    for (i=0;i<faults.count && shown<128;++i) {
        if (!fault_record_at(&faults,i,&record)) return;
        if (!(record.status & record.available & fault_status)) continue;
        printf("%06lX  status %02X",(unsigned long)record.code,record.status);
        if (status_mask) printf(" mask %02X",record.available);
        putchar('\n');
        ++shown;
    }
    if (count>128) puts("More codes returned; first 128 shown");
    if (full) puts("More codes may exist");
}

int main(int argc, char **argv) {
    const char *host=0;
    unsigned port=6801;
    int mode=0, transport=DIAG_AUTO, trace=0, confirm_clear=0, status_mask=0, i;
    struct diag_link link;
    struct diag_link_cfg cfg;
    for (i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && i+1<argc && !host) host=argv[++i];
        else if (!strcmp(argv[i],"--port") && i+1<argc) port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"auto")) ++i;
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"hsfz")) { transport=DIAG_HSFZ; ++i; }
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"most")) { transport=DIAG_MOST; ++i; }
        else if (!strcmp(argv[i],"--trace")) trace=1;
        else if (!strcmp(argv[i],"--status-mask") && !status_mask) status_mask=1;
        else if (!strcmp(argv[i],"--confirm-clear") && !confirm_clear) confirm_clear=1;
        else if (!strcmp(argv[i],"--codes") && !mode) mode=1;
        else if (!strcmp(argv[i],"--clear") && !mode) mode=2;
        else break;
    }
    if (!mode) mode=1; // read-only is the default operation
    if (i!=argc || (mode==2 && !confirm_clear) || (confirm_clear && mode!=2) ||
        (trace && mode!=1) || (transport==DIAG_HSFZ && !host)) {
        fprintf(stderr,"Usage: %s [--host IPv4] [--transport auto|hsfz|most] [--port 6801] [--status-mask] [--codes [--trace] | --clear --confirm-clear]\n",argv[0]);
        return 2;
    }
    diag_link_signals();
    memset(&cfg,0,sizeof cfg);
    cfg.host=host; cfg.port=port; cfg.transport=transport; cfg.kind=DIAG_ONESHOT; cfg.dsi_name="enginecodes";
    diag_link_init(&link,&cfg);
    if (!diag_link_reserve_oneshot(&link)) {
        puts(errno==EADDRINUSE ? "Diagnostics busy. Wait a moment and try again."
                               : "Could not reserve the diagnostics connection");
        return 0;
    }
    fault_codes(&link,mode==2,trace,status_mask);
    result_done();
    diag_link_close(&link);
    return 0;
}
