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
    const char *host = 0;
    unsigned port = 6801, mode = 3, negative;
    int i, n, transport = DIAG_AUTO;
    struct diag_link link;
    struct diag_link_cfg cfg;
    static const unsigned char requests[][6] = {
        {0x2f, 0x60, 0xfd, 0},
        {0x2f, 0x60, 0xfd, 3, 0x14, 0xfe},
        {0x2f, 0x60, 0xfd, 3, 0x23, 0x28}
    };
    static const char *labels[] = {"Exhaust auto", "Exhaust open", "Exhaust closed"};
    const unsigned char prefix[] = {0x6f, 0x60, 0xfd};
    unsigned char reply[64];

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--host") && i+1 < argc && !host) host = argv[++i];
        else if (!strcmp(argv[i], "--port") && i+1 < argc) port = number(argv[++i], 1, 65535);
        else if (!strcmp(argv[i], "--flap") && i+1 < argc && mode == 3) mode = number(argv[++i], 0, 2);
        else if (!strcmp(argv[i], "--transport") && i+1 < argc && !strcmp(argv[i+1], "hsfz")) {
            transport = DIAG_HSFZ; ++i;
        } else if (!strcmp(argv[i], "--transport") && i+1 < argc && !strcmp(argv[i+1], "most")) {
            transport = DIAG_MOST; ++i;
        } else return 2;
    }
    if (mode == 3 || (!host && transport == DIAG_HSFZ)) return 2;
    diag_link_signals();
    memset(&cfg, 0, sizeof cfg);
    cfg.host = host;
    cfg.port = port;
    cfg.transport = transport;
    cfg.kind = DIAG_ONESHOT;
    cfg.dsi_name = "exhaustflap";
    diag_link_init(&link, &cfg);
    if (!diag_link_reserve_oneshot(&link)) {
        puts("Diagnostics busy. Wait a moment and try again.");
        return 0;
    }
    if (diag_link_up(&link)) {
        n = diag_link_request(&link, 0x12, requests[mode], mode ? 6u : 4u,
                              prefix, sizeof prefix, reply, sizeof reply, 1600, &negative);
        if (n >= 3) printf("Confirmed: %s\n", labels[mode]);
        else if (n == -2) printf("%s refused by DME (NRC %02X)\n", labels[mode], negative);
        else printf("%s: no DME reply\n", labels[mode]);
    } else puts("Diagnostic gateway unavailable");
    result_done();
    diag_link_close(&link);
    return 0;
}
