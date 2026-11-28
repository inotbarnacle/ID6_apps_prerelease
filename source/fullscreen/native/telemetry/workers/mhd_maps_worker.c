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

#include <fcntl.h>

/* The result is out: end the plugin's pipe now, before the link's bounded close. */
static void result_done(void) {
    int null;
    fflush(stdout);
    null = open("/dev/null", O_WRONLY);
    if (null >= 0) { dup2(null, 1); close(null); }
}

int main(int argc, char **argv) {
    const char *host=0;
    unsigned port=6801, function=0, value=0, negative;
    int n, i, transport=DIAG_AUTO;
    struct diag_link link;
    struct diag_link_cfg cfg;
    unsigned char request[]={0x2f,0,0}, prefix[]={0x6f,0,0}, reply[16];
    const char *label = 0;
    const char *flap[] = {"Exhaust OEM", "Exhaust open", "Exhaust closed"};
    for (i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && i+1<argc && !host) host=argv[++i];
        else if (!strcmp(argv[i],"--port") && i+1<argc) port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"auto")) ++i;
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"hsfz")) { transport=DIAG_HSFZ; ++i; }
        else if (!strcmp(argv[i],"--transport") && i+1<argc && !strcmp(argv[i+1],"most")) { transport=DIAG_MOST; ++i; }
        else if (!strcmp(argv[i],"--map") && i+1<argc && !function) {
            function=1; value=number(argv[++i],1,4)-1;
        }
        else if (!strcmp(argv[i],"--flap") && i+1<argc && !function) {
            function=2; value=number(argv[++i],0,2); label=flap[value];
        } else if (!strcmp(argv[i],"--cooling") && i+1<argc && !function) {
            function=3; value=number(argv[++i],0,1); label=value?"Max Cooling on":"Max Cooling off";
        }
        else return 2;
    }
    if (!function || (transport==DIAG_HSFZ && !host)) return 2;
    diag_link_signals();
    memset(&cfg,0,sizeof cfg);
    cfg.host=host; cfg.port=port; cfg.transport=transport; cfg.kind=DIAG_ONESHOT; cfg.dsi_name="mhdmaps";
    diag_link_init(&link,&cfg);
    if (!diag_link_reserve_oneshot(&link)) { puts("Diagnostics busy. Wait a moment and try again."); return 0; }
    if (!diag_link_up(&link)) {
        puts("Diagnostic gateway unavailable");
        result_done();
        diag_link_close(&link);
        return 0;
    }
    /* MHD+ live controls documented in idrive7-apps at 9f0f68a.
       The matching echo acknowledges the request. */
    if (function==3 && value) {
        const unsigned char rpm[]={0x01,0x0c}, rpm_reply[]={0x41,0x0c};
        n=diag_link_request(&link,0x12,rpm,sizeof rpm,rpm_reply,sizeof rpm_reply,reply,sizeof reply,500,&negative);
        if (n!=4 || (((unsigned)reply[2]<<8)|reply[3])<=2000 ||
            (((unsigned)reply[2]<<8)|reply[3])>40000) {
            puts("Start the engine first. Max Cooling ON needs live RPM.");
            result_done();
            diag_link_close(&link);
            return 0;
        }
    }
    request[1]=prefix[1]=(unsigned char)function;
    request[2]=prefix[2]=(unsigned char)value;
    n=diag_link_request(&link,0x12,request,sizeof request,prefix,sizeof prefix,reply,sizeof reply,1600,&negative);
    if (label) {
        if (n>=3) printf("Accepted by DME: %s\n",label);
        else if (n==-2) printf("%s refused by DME (NRC %02X)\n",label,negative);
        else printf("%s: no matching DME reply\n",label);
    } else if (n>=3) printf("Accepted by DME: Map %u\n",value+1);
    else if (n==-2) printf("Map %u refused by DME (NRC %02X)\n",value+1,negative);
    else puts("Map result unknown. Check Current Map in MHD.");
    result_done();
    diag_link_close(&link);
    return 0;
}
