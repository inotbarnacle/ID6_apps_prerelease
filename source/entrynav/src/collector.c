#define _POSIX_C_SOURCE 200809L
#include "gauge_core.h"
#include "diagnostic_lease.h"
#include "paths.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }

static int ready(int fd, short events, uint64_t deadline) {
    struct pollfd p;
    int rc;
    p.fd = fd; p.events = events; p.revents = 0;
    while (running) {
        uint64_t now = bd_mono_ms();
        if (now >= deadline) return 0;
        rc = poll(&p, 1, (int)(deadline - now));
        if (rc < 0 && errno == EINTR) continue;
        return rc > 0 && (p.revents & events);
    }
    return 0;
}
static int transfer(int fd, unsigned char *buf, size_t n, int sending, uint64_t deadline) {
    while (n && running) {
        ssize_t k;
        if (!ready(fd, sending ? POLLOUT : POLLIN, deadline)) return 0;
        k = sending ? send(fd, buf, n, 0) : recv(fd, buf, n, 0);
        if (k < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (k <= 0) return 0;
        buf += k; n -= (size_t)k;
    }
    return n == 0;
}
static int connect_gw(const char *host, unsigned port) {
    struct sockaddr_in a;
    int fd, error = 0, no_delay = 1;
    socklen_t len = sizeof error;
    memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) != 1) return -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof no_delay);
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) {
        close(fd); return -1;
    }
    if (!ready(fd, POLLOUT, bd_mono_ms() + 1000) ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) || error) {
        close(fd); return -1;
    }
    return fd;
}

/* The fixed callers below supply the UDS service, target, and response prefix. */
static int exchange(int fd, unsigned ecu, const unsigned char *uds, unsigned uds_size,
                    const unsigned char *prefix, unsigned prefix_size,
                    unsigned char *reply, unsigned reply_size, unsigned timeout_ms,
                    unsigned *negative) {
    /* HSFZ header (length, control 1, tester F4, target) and up to 64 UDS bytes; BM3's longest request is 11. */
    unsigned char request[8+64] = {0,0,0,0,0,1,0xf4,0};
    unsigned char header[6], body[4096];
    uint64_t deadline = bd_mono_ms() + timeout_ms;
    if (uds_size > sizeof request - 8) return -1;
    request[3] = (unsigned char)(uds_size+2);
    request[7] = (unsigned char)ecu;
    memcpy(request+8,uds,uds_size);
    *negative=0;
    if (!transfer(fd,request,8+uds_size,1,deadline)) return -1;
    while (running) {
        uint32_t len;
        unsigned ctrl;
        if (!transfer(fd,header,6,0,deadline)) return -1;
        len=((uint32_t)header[0]<<24)|((uint32_t)header[1]<<16)|((uint32_t)header[2]<<8)|header[3];
        ctrl=((unsigned)header[4]<<8)|header[5];
        if (len>sizeof body || !transfer(fd,body,len,0,deadline)) return -1;
        if (ctrl==0x12) {
            unsigned char alive[8]={0,0,0,2,0,0x13,0,0xf4};
            if (!transfer(fd,alive,8,1,deadline)) return -1;
            continue;
        }
        if (ctrl!=1 || len<3 || body[0]!=ecu || body[1]!=0xf4) continue;
        if (len==5 && body[2]==0x7f && body[3]==uds[0]) {
            if (body[4]==0x78) continue;
            *negative=body[4];
            return -2;
        }
        if (len-2<prefix_size || memcmp(body+2,prefix,prefix_size)) continue;
        if (len-2>reply_size) return -1;
        memcpy(reply,body+2,len-2);
        return (int)(len-2);
    }
    return -1;
}

static unsigned number(const char *s, unsigned lo, unsigned hi) {
    char *end; unsigned long n = strtoul(s, &end, 10);
    if (!*s || *end || n < lo || n > hi) { fprintf(stderr, "Invalid number: %s\n", s); exit(2); }
    return (unsigned)n;
}

/* ---- finding the gateway: ena-gateway discover, and each worker when the saved address stops answering ---- */
#define MAX_NETWORKS 8
#define MAX_GATEWAYS 16

struct network { uint32_t address, mask; };

/* The IPv4 networks on device: the interface itself and its labelled aliases (vbr0:1), not its VLANs (vbr0.73). */
static inline unsigned device_networks(const char *device, struct network *networks, unsigned capacity) {
    struct ifaddrs *list, *i;
    size_t length = strlen(device);
    unsigned count = 0;
    if (getifaddrs(&list) < 0) { perror("getifaddrs"); return 0; }
    for (i = list; i && count < capacity; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_netmask) continue;
        if (strncmp(i->ifa_name, device, length) || (i->ifa_name[length] && i->ifa_name[length] != ':')) continue;
        networks[count].address = ntohl(((struct sockaddr_in *)i->ifa_addr)->sin_addr.s_addr);
        networks[count].mask = ntohl(((struct sockaddr_in *)i->ifa_netmask)->sin_addr.s_addr);
        ++count;
    }
    freeifaddrs(list);
    return count;
}

static inline int on_networks(struct in_addr address, const struct network *networks, unsigned count) {
    uint32_t host = ntohl(address.s_addr);
    unsigned i;
    for (i = 0; i < count; ++i)
        if ((host & networks[i].mask) == (networks[i].address & networks[i].mask)) return 1;
    return 0;
}

/* HSFZ vehicle identification: sends 00 00 00 00 00 11 to the broadcast address of every IPv4 network on device and
 * collects each responder from those networks that calls itself the gateway (DIAGADR10), up to capacity. With to, it
 * sends there instead and accepts any responder (the tests). The gateway is usually link-local; while tethering or a
 * laptop on the ENET cable hands out addresses, the gateway and the head unit take one, and the gateway keeps it for
 * a while after it goes (seen on an F20, 15 November 2026). Returns the count, -1 when device has no address, -2 when
 * nothing could be sent. */
static inline int find_gateways(const char *device, const char *to, unsigned port, unsigned wait_ms,
                                char found[][INET_ADDRSTRLEN], unsigned capacity) {
    const unsigned char request[] = {0, 0, 0, 0, 0, 0x11};
    struct network networks[MAX_NETWORKS];
    struct sockaddr_in dest, from;
    unsigned char reply[2048];
    unsigned count = 0, sent = 0, n_found = 0, i;
    int on = 1;
    uint64_t deadline;
    int fd;
    memset(&dest, 0, sizeof dest);
    dest.sin_family = AF_INET;
    dest.sin_port = htons((uint16_t)port);
    if (to && inet_pton(AF_INET, to, &dest.sin_addr) != 1) { fprintf(stderr, "Invalid address: %s\n", to); return -2; }
    if (!to) {
        count = device_networks(device, networks, MAX_NETWORKS);
        if (!count) { fprintf(stderr, "%s has no IPv4 address\n", device); return -1; }
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return -2; }
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    for (i = 0; i < (to ? 1u : count); ++i) {
        if (!to) dest.sin_addr.s_addr = htonl(networks[i].address | ~networks[i].mask);
        if (sendto(fd, request, sizeof request, 0, (struct sockaddr *)&dest, sizeof dest) < 0) {
            char text[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &dest.sin_addr, text, sizeof text);
            fprintf(stderr, "sendto %s: %s\n", text, strerror(errno));
        } else ++sent;
    }
    if (!sent) { close(fd); return -2; }
    deadline = bd_mono_ms() + wait_ms;
    while (running && n_found < capacity && ready(fd, POLLIN, deadline)) {
        socklen_t length = sizeof from;
        ssize_t n = recvfrom(fd, reply, sizeof reply - 1, 0, (struct sockaddr *)&from, &length);
        char text[INET_ADDRSTRLEN];
        int duplicate = 0;
        if (n < 6 || reply[4] != 0 || reply[5] != 0x11) continue;
        if (!to && !on_networks(from.sin_addr, networks, count)) continue;
        reply[n] = 0;
        /* The identification is text (DIAGADR10BMWMAC...BMWVIN...); the gateway's diagnostic address is 10. */
        for (i = 6; i + 9 <= (unsigned)n; ++i)
            if (!memcmp(reply + i, "DIAGADR10", 9)) break;
        if (i + 9 > (unsigned)n) continue;
        inet_ntop(AF_INET, &from.sin_addr, text, sizeof text);
        for (i = 0; i < n_found; ++i) duplicate |= !strcmp(found[i], text);
        if (!duplicate) strcpy(found[n_found++], text);
    }
    close(fd);
    return (int)n_found;
}

/* Saves address for every app, as setup.sh does: written whole, then renamed into place. */
static inline int save_gateway(const char *path, const char *address) {
    char temporary[256];
    FILE *file;
    int written;
    if (snprintf(temporary, sizeof temporary, "%s.new", path) >= (int)sizeof temporary) return 0;
    if (!(file = fopen(temporary, "w"))) return 0;
    written = fprintf(file, "%s\n", address) > 0;
    written = fflush(file) == 0 && fsync(fileno(file)) == 0 && written;
    if (fclose(file) || !written || rename(temporary, path)) { unlink(temporary); return 0; }
    return 1;
}

/* A worker's search for the gateway when the saved address stops answering: the gateway moves to an address that
 * tethering hands it, and back once that runs out. --search-dev, --search-port and --gateway-file are for the tests. */
enum { GATEWAY_SEARCH_FAILURES = 3, GATEWAY_SEARCH_MS = 20000, GATEWAY_SEARCH_WAIT_MS = 1200 };
struct gateway_search {
    const char *device, *file;
    unsigned port, failures;
    uint64_t next;
};

static inline void gateway_search_init(struct gateway_search *search) {
    search->device = "vbr0";
    search->file = ENA_GATEWAY_FILE;
    search->port = 6811;
    search->failures = 0;
    search->next = 0;
}

/* Takes --search-dev, --search-port or --gateway-file at argv[*i]; 1 if it was one of them. */
static inline int gateway_search_option(struct gateway_search *search, int argc, char **argv, int *i) {
    if (*i + 1 >= argc) return 0;
    if (!strcmp(argv[*i], "--search-dev")) search->device = argv[++*i];
    else if (!strcmp(argv[*i], "--search-port")) search->port = number(argv[++*i], 1, 65535);
    else if (!strcmp(argv[*i], "--gateway-file")) search->file = argv[++*i];
    else return 0;
    return 1;
}

/* Called after each failed connection to host (16 bytes). After GATEWAY_SEARCH_FAILURES in a row, and at most every
 * GATEWAY_SEARCH_MS, it asks the car-side Ethernet for the gateway. The first other address that takes a connection
 * on hsfz_port replaces host and is saved for the other apps. Returns 1 when host changed. */
static inline int gateway_search_after_failure(struct gateway_search *search, char *host, unsigned hsfz_port) {
    char found[MAX_GATEWAYS][INET_ADDRSTRLEN];
    struct in_addr address;
    int n, i;
    uint64_t now = bd_mono_ms();
    if (++search->failures < GATEWAY_SEARCH_FAILURES || now < search->next) return 0;
    search->next = now + GATEWAY_SEARCH_MS;
    n = find_gateways(search->device, 0, search->port, GATEWAY_SEARCH_WAIT_MS, found, MAX_GATEWAYS);
    for (i = 0; i < n; ++i) {
        int fd;
        if (!strcmp(found[i], host) || inet_pton(AF_INET, found[i], &address) != 1) continue;
        /* The tests search lo, where every address is loopback. */
        if (!gateway_address_ok(ntohl(address.s_addr)) &&
            !(!strcmp(search->device, "lo") && ntohl(address.s_addr) >> 24 == 127))
            continue;
        fd = connect_gw(found[i], hsfz_port);
        if (fd < 0) continue;
        close(fd);
        fprintf(stderr, "The gateway moved from %s to %s\n", host, found[i]);
        strcpy(host, found[i]);
        save_gateway(search->file, found[i]);
        search->failures = 0;
        return 1;
    }
    return 0;
}

#ifdef ENGINE_CODES_ONLY
/* engine-codes: the DME's fault memory, for the Engine Fault Codes app. It prints a header line, then one line per
 * code, "XXXXXX  status SS", which the app pages through and describes (src/dme_descriptions.h).
 *   engine-codes --host IPv4 --codes   reads the codes; only reads
 *   engine-codes --host IPv4 --clear   clears the fault memory (UDS 14 FF FF FF), then reads the codes again
 * --search-dev, --search-port and --gateway-file are for the tests, as in the other workers. */
static void fault_codes(int fd, int clear) {
    /* Fault, pending, confirmed, failed since clear, or warning requested.
     * Bits 0x10 and 0x40 only report incomplete tests. */
    enum { fault_status = 0xaf };
    const unsigned char read_request[]={0x19,0x02,fault_status}, read_reply[]={0x59,0x02};
    const unsigned char clear_request[]={0x14,0xff,0xff,0xff}, clear_reply[]={0x54};
    unsigned char reply[4094];
    unsigned negative;
    unsigned supported_fault_status;
    int n, i, count=0, shown=0;
    if (fd<0) { puts("Diagnostic gateway unavailable"); return; }
    if (clear) {
        n=exchange(fd,0x12,clear_request,4,clear_reply,1,reply,sizeof reply,8000,&negative);
        /* A DME that wants the extended session for a clear: open it, clear once more and go back to the default
         * session. Only after a session-related refusal, and only when the DME accepts the session. */
        if (n==-2 && (negative==0x22 || negative==0x31 || negative==0x7e || negative==0x7f)) {
            const unsigned char extended[]={0x10,0x03}, extended_reply[]={0x50,0x03};
            const unsigned char normal[]={0x10,0x01}, normal_reply[]={0x50,0x01};
            unsigned session_negative;
            if (exchange(fd,0x12,extended,2,extended_reply,2,reply,sizeof reply,2000,&session_negative)>=2) {
                n=exchange(fd,0x12,clear_request,4,clear_reply,1,reply,sizeof reply,8000,&negative);
                exchange(fd,0x12,normal,2,normal_reply,2,reply,sizeof reply,2000,&session_negative);
            }
        }
        if (n!=1) {
            if (n==-2 && (negative==0x22 || negative==0x83 || negative==0x88))
                printf("Clear refused (NRC %02X). Try with the ignition on and the engine off.\n",negative);
            else if (n==-2) printf("Clear rejected (NRC %02X)\n",negative);
            else puts("Clear result unknown: read codes again");
            return;
        }
    }
    n=exchange(fd,0x12,read_request,3,read_reply,2,reply,sizeof reply,1800,&negative);
    if (n==-2) { printf("Read rejected (NRC %02X)\n",negative); return; }
    if (n<3 || (n-3)%4!=0) { puts(clear ? "Clear acknowledged; recheck unavailable" : "No valid code response"); return; }
    supported_fault_status=reply[2] & fault_status;
    for (i=3;i<n;i+=4)
        if (reply[i+3] & supported_fault_status) ++count;
    printf("Engine: %d %s%s\n",count,
           count==1 ? "code" : "codes",clear ? " after clear" : "");
    for (i=3;i<n && shown<128;i+=4) {
        if (!(reply[i+3] & supported_fault_status)) continue;
        printf("%02X%02X%02X  status %02X\n",reply[i],reply[i+1],reply[i+2],reply[i+3]);
        ++shown;
    }
    if (count>128) puts("More codes returned; first 128 shown");
}

int main(int argc, char **argv) {
    const char *host_option=0;
    char host[INET_ADDRSTRLEN]="";
    unsigned port=6801;
    int mode=0, lock, gw, i;
    uint64_t deadline;
    struct gateway_search search;
    gateway_search_init(&search);
    for (i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && i+1<argc && !host_option) host_option=argv[++i];
        else if (!strcmp(argv[i],"--port") && i+1<argc) port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--codes") && !mode) mode=1;
        else if (!strcmp(argv[i],"--clear") && !mode) mode=2;
        else if (!gateway_search_option(&search,argc,argv,&i)) break;
    }
    if (host_option && strlen(host_option)<sizeof host) strcpy(host,host_option);
    if (i!=argc || !host[0] || !mode) {
        fprintf(stderr,"Usage: %s --host IPv4 [--port 6801] (--codes | --clear)\n",argv[0]);
        return 2;
    }
    signal(SIGINT,stop); signal(SIGTERM,stop); signal(SIGPIPE,SIG_IGN);
    /* A gauge app still holding the gateway gives it up within a lease once asked (reserve_gateway asks). */
    lock=reserve_gateway();
    deadline=bd_mono_ms()+650;
    while (running && lock<0 && errno==EADDRINUSE && bd_mono_ms()<deadline) {
        poll(0,0,25);
        lock=reserve_gateway();
    }
    if (lock<0) {
        puts(errno==EADDRINUSE ? "Diagnostics busy. Wait a moment and try again."
                               : "Could not reserve the diagnostics connection");
        return 0;
    }
    gw=connect_gw(host,port);
    if (gw<0) {
        search.failures=GATEWAY_SEARCH_FAILURES-1; /* a one-shot run searches at once */
        if (gateway_search_after_failure(&search,host,port)) gw=connect_gw(host,port);
    }
    fault_codes(gw,mode==2);
    release_gateway(&gw,&lock);
    return 0;
}
#elif defined(BM3_MAPS_ONLY)
/* bm3-maps: bootmod3 CustomROM controls (src/bm3.h). It prints one line for the app's status label.
 *   bm3-maps --host IPv4 --status       identifies the ROM and reads the active slot; only reads
 *   bm3-maps --host IPv4 --map 1..4     identifies the ROM, writes the slot and reads it back
 *   bm3-maps --host IPv4 --sound 0|1    Active Sound off or on
 *   bm3-maps --host IPv4 --burbles 0..3 burbles off, or aggression 4, 8 or 12; written and read back
 * Every run identifies the ROM again, and nothing is written unless the marker names a known ROM.
 * --marker HEX sets the marker's address; otherwise ENA_DATA_DIR/bm3-marker-address, otherwise each address bootmod3's
 * own app tries (bm3_marker_addresses). --dump shows each step and the marker's bytes on stderr; it only adds output.
 * Exit 0 when the line reports the active map, 1 otherwise. */
#include "bm3.h"
#include "paths.h"

/* bm3_protocol.js's timeouts: 4.5 s for most requests and 8 s for Active Sound. */
static int bm3_exchange_within(int fd, const unsigned char *uds, size_t size, unsigned char *reply, unsigned cap,
                               unsigned *negative, unsigned timeout_ms) {
    unsigned char prefix[8];
    size_t length;
    if (!size) { /* a request builder refused its arguments */
        *negative = 0;
        return -1;
    }
    length = bm3_reply_prefix(uds, prefix);
    return exchange(fd, BM3_DME, uds, (unsigned)size, prefix, (unsigned)length, reply, cap, timeout_ms, negative);
}

static int bm3_exchange(int fd, const unsigned char *uds, size_t size, unsigned char *reply, unsigned cap,
                        unsigned *negative) {
    return bm3_exchange_within(fd, uds, size, reply, cap, negative, 4500);
}

static int bm3_dumping = 0; /* --dump: each identification step's result and the marker's bytes, on stderr */

static void bm3_note(const char *step, int n, unsigned negative) {
    if (!bm3_dumping) return;
    if (n == -2) fprintf(stderr, "%s: refused, NRC %02X\n", step, negative);
    else if (n < 0) fprintf(stderr, "%s: no answer\n", step);
    else fprintf(stderr, "%s: %d bytes\n", step, n);
}

static void bm3_dump(const unsigned char *data, size_t size) {
    size_t i, j;
    if (!bm3_dumping) return;
    for (i = 0; i < size; i += 16) {
        fprintf(stderr, "%04x ", (unsigned)i);
        for (j = i; j < i + 16; ++j) {
            if (j < size) fprintf(stderr, " %02x", data[j]);
            else fputs("   ", stderr);
        }
        fputs("  ", stderr);
        for (j = i; j < i + 16 && j < size; ++j) fputc(data[j] >= 0x20 && data[j] < 0x7f ? data[j] : '.', stderr);
        fputc('\n', stderr);
    }
}

/* Reads the CustomROM marker at one address: through a dynamic DID, or straight from memory when the DME refuses
 * that. On failure it writes the reason to message: kind 1 for a refusal (try the next address), 2 for a marker that
 * names no known ROM, 3 when the DME stopped answering. */
static const bm3_profile *bm3_identify(int fd, uint32_t marker_address, char *message, size_t cap, int *kind) {
    unsigned char request[16], reply[1024]; /* room for a wrong-length marker, reported as such */
    const unsigned char *marker = 0;
    size_t marker_size = 0;
    const char *why;
    const bm3_profile *profile;
    unsigned negative;
    int n, refused = 0;
    if (bm3_dumping) fprintf(stderr, "marker at %08lX\n", (unsigned long)marker_address);
    n = bm3_exchange(fd, request, bm3_clear_dynamic_did(request), reply, sizeof reply, &negative);
    bm3_note("2C 03 F3 01", n, negative);
    if (n == -2) refused = 1;
    else if (n < 0) goto lost;
    if (!refused) {
        n = bm3_exchange(fd, request, bm3_define_marker_did(request, marker_address), reply, sizeof reply, &negative);
        bm3_note("2C 02 F3 01", n, negative);
        if (n == -2) refused = 1;
        else if (n < 0) goto lost;
    }
    if (!refused) {
        n = bm3_exchange(fd, request, bm3_read_marker_did(request), reply, sizeof reply, &negative);
        bm3_note("22 F3 01", n, negative);
        if (n == -2) refused = 1;
        else if (n < 0) goto lost;
        else { marker = reply + 3; marker_size = (size_t)n - 3; } /* after 62 F3 01 */
    }
    if (refused) {
        n = bm3_exchange(fd, request, bm3_read_memory(request, marker_address, BM3_MARKER_SIZE), reply, sizeof reply,
                         &negative);
        bm3_note("23 14", n, negative);
        if (n == -2) {
            snprintf(message, cap, "The DME refused to read its memory (NRC %02X)", negative);
            *kind = 1;
            return 0;
        }
        if (n < 0) goto lost;
        marker = reply + 1; /* after 63 */
        marker_size = (size_t)n - 1;
    }
    bm3_dump(marker, marker_size);
    profile = bm3_marker_profile(marker, marker_size, &why);
    if (!profile) {
        snprintf(message, cap, "No known bootmod3 ROM: %s", why);
        *kind = 2;
    }
    return profile;
lost:
    snprintf(message, cap, "The DME did not answer");
    *kind = 3;
    return 0;
}

/* Where bootmod3's own app looks for the marker, in its order: MG1 first, then the F-series MEVD17 (0977FE4C, seen
 * on an F20 M140i with R1C900000) and two more DME families. Captured by Noahbg/mgu-apps (hu-obd-adapter). */
static const uint32_t bm3_marker_addresses[] = {0x807FFE50u, 0x0977FE4Cu, 0x801FFDACu, 0x8005FE1Eu};

/* The profile at the first address whose marker names a known ROM. The status line keeps the most telling failure:
 * a marker read beats a refusal, and a lost DME ends the search. */
static const bm3_profile *bm3_find(int fd, const uint32_t *addresses, size_t count) {
    char message[96] = "No bootmod3 CustomROM marker found", attempt[96];
    int best = 0, kind;
    size_t i;
    for (i = 0; i < count; ++i) {
        const bm3_profile *profile;
        kind = 0;
        profile = bm3_identify(fd, addresses[i], attempt, sizeof attempt, &kind);
        if (profile) return profile;
        if (kind >= best) { best = kind; snprintf(message, sizeof message, "%s", attempt); }
        if (kind == 3) break;
    }
    puts(message);
    return 0;
}

static int bm3_read_slot(int fd, const bm3_profile *profile) {
    unsigned char request[16], reply[8];
    unsigned negative;
    int n = bm3_exchange(fd, request, bm3_read_memory(request, profile->map_address, 1), reply, sizeof reply,
                         &negative);
    return n > 0 ? bm3_slot_reply(reply, (size_t)n) : -1;
}

/* Writes size bytes at address with the profile's memory-write service; the exchange's result. */
static int bm3_write(int fd, const bm3_profile *profile, uint32_t address, const unsigned char *data, unsigned size,
                     unsigned *negative) {
    unsigned char request[8 + BM3_BURBLE_TABLE], reply[8];
    return bm3_exchange(fd, request, bm3_write_memory(request, sizeof request, profile, address, data, size),
                        reply, sizeof reply, negative);
}

/* Whether the DME's memory at address reads back as data. */
static int bm3_holds(int fd, uint32_t address, const unsigned char *data, unsigned size) {
    unsigned char request[16], reply[8 + BM3_BURBLE_TABLE];
    unsigned negative;
    int n = bm3_exchange(fd, request, bm3_read_memory(request, address, size), reply, sizeof reply, &negative);
    return n == (int)size + 1 && !memcmp(reply + 1, data, size);
}

/* Map slot 1..4: written with its activation flag, then read back. Exit status for main. */
static int bm3_set_map(int fd, const bm3_profile *profile, unsigned slot) {
    const unsigned char data[2] = {(unsigned char)(slot - 1), 0x01}; /* the slot, then activate */
    unsigned negative;
    int active;
    if (bm3_write(fd, profile, profile->map_address, data, sizeof data, &negative) == -2) {
        printf("The DME refused the map change (NRC %02X)\n", negative);
        return 1;
    }
    active = bm3_read_slot(fd, profile);
    if (active < 0) {
        puts("Map change not confirmed. Check the active map before trying again.");
        return 1;
    }
    if ((unsigned)active != slot - 1) {
        printf("Map change not confirmed: the DME reports map %d\n", active + 1);
        return 1;
    }
    printf("Map %d active\n", active + 1);
    return 0;
}

/* Burble preset 0..3 (bm3_burble_table). Off stops the DME using the table before clearing it; on fills the table
 * before turning it on. Both are read back. */
static int bm3_set_burbles(int fd, const bm3_profile *profile, unsigned preset) {
    unsigned char table[BM3_BURBLE_TABLE];
    const unsigned char enabled = preset != 0;
    unsigned negative = 0;
    int n;
    if (!bm3_burble_table(profile, preset, table)) {
        puts("No burble table for this ROM");
        return 1;
    }
    if (enabled) {
        n = bm3_write(fd, profile, profile->timing_address, table, sizeof table, &negative);
        if (n > 0) n = bm3_write(fd, profile, profile->burble_address, &enabled, 1, &negative);
    } else {
        n = bm3_write(fd, profile, profile->burble_address, &enabled, 1, &negative);
        if (n > 0) n = bm3_write(fd, profile, profile->timing_address, table, sizeof table, &negative);
    }
    if (n == -2) {
        printf("The DME refused the burble change (NRC %02X)\n", negative);
        return 1;
    }
    if (!bm3_holds(fd, profile->timing_address, table, sizeof table) ||
        !bm3_holds(fd, profile->burble_address, &enabled, 1)) {
        puts("Burble change not confirmed. Try again before driving.");
        return 1;
    }
    if (preset) printf("Burbles at aggression %u, read back from the DME\n", preset * BM3_BURBLE_STEP);
    else puts("Burbles off, read back from the DME");
    return 0;
}

/* Active Sound off (0) or on (1): one request the DME acknowledges. It can't be read back. */
static int bm3_set_sound(int fd, unsigned on) {
    unsigned char request[16], reply[16];
    unsigned negative;
    const char *what = on ? "Active Sound on" : "Active Sound off";
    int n = bm3_exchange_within(fd, request, bm3_sound_request(request, (int)on), reply, sizeof reply, &negative,
                                8000);
    if (n > 0) {
        printf("The DME accepted: %s\n", what);
        return 0;
    }
    if (n == -2) printf("The DME refused %s (NRC %02X)\n", what, negative);
    else printf("No answer from the DME for %s\n", what);
    return 1;
}

/* The marker address set by hand (--marker, or ENA_DATA_DIR/bm3-marker-address): 1 and *address, or 0 for none. */
static int bm3_marker_address(const char *option, uint32_t *address) {
    char text[32] = "";
    char *end;
    unsigned long value;
    FILE *file;
    if (option) snprintf(text, sizeof text, "%s", option);
    else if ((file = fopen(ENA_DATA_DIR "/bm3-marker-address", "r"))) {
        if (!fgets(text, sizeof text, file)) text[0] = 0;
        fclose(file);
    }
    text[strcspn(text, "\r\n")] = 0;
    if (!text[0]) return 0;
    value = strtoul(text, &end, 16);
    if (strlen(text) != 8 || *end) { fprintf(stderr, "Invalid marker address: %s\n", text); exit(2); }
    *address = (uint32_t)value;
    return 1;
}

int main(int argc, char **argv) {
    enum { NONE, STATUS, MAP, SOUND, BURBLES } action = NONE;
    const char *host_option = 0, *marker_option = 0;
    char host[INET_ADDRSTRLEN] = "";
    unsigned port = 6801, value = 0;
    int fd, lock, i, active, marker_set, result;
    uint32_t marker_address = 0;
    const bm3_profile *profile;
    struct gateway_search search;
    gateway_search_init(&search);
    for (i = 1; i < argc; ++i) {
        int has_value = i + 1 < argc && action == NONE;
        if (!strcmp(argv[i], "--host") && i + 1 < argc && !host_option) host_option = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = number(argv[++i], 1, 65535);
        else if (!strcmp(argv[i], "--marker") && i + 1 < argc) marker_option = argv[++i];
        else if (!strcmp(argv[i], "--status") && action == NONE) action = STATUS;
        else if (!strcmp(argv[i], "--map") && has_value) { action = MAP; value = number(argv[++i], 1, BM3_SLOTS); }
        else if (!strcmp(argv[i], "--sound") && has_value) { action = SOUND; value = number(argv[++i], 0, 1); }
        else if (!strcmp(argv[i], "--burbles") && has_value) {
            action = BURBLES;
            value = number(argv[++i], 0, BM3_BURBLE_PRESETS);
        }
        else if (!strcmp(argv[i], "--dump")) bm3_dumping = 1;
        else if (!gateway_search_option(&search, argc, argv, &i)) return 2;
    }
    if (host_option && strlen(host_option) < sizeof host) strcpy(host, host_option);
    if (!host[0] || action == NONE) {
        fprintf(stderr, "Usage: %s --host IPv4 [--port 6801] [--marker HEX] [--dump] "
                        "(--status | --map 1-4 | --sound 0-1 | --burbles 0-3)\n", argv[0]);
        return 2;
    }
    marker_set = bm3_marker_address(marker_option, &marker_address);
    signal(SIGINT, stop); signal(SIGTERM, stop); signal(SIGPIPE, SIG_IGN);
    lock = reserve_gateway();
    if (lock < 0) { puts("Diagnostics busy. Wait a moment and try again."); return 1; }
    fd = connect_gw(host, port);
    if (fd < 0) {
        search.failures = GATEWAY_SEARCH_FAILURES - 1; /* a one-shot run searches at once */
        if (gateway_search_after_failure(&search, host, port)) fd = connect_gw(host, port);
    }
    if (fd < 0) { puts("Diagnostic gateway unavailable"); close(lock); return 1; }
    profile = marker_set ? bm3_find(fd, &marker_address, 1)
                         : bm3_find(fd, bm3_marker_addresses, sizeof bm3_marker_addresses / sizeof *bm3_marker_addresses);
    if (!profile) { release_gateway(&fd, &lock); return 1; }
    fprintf(stderr, "ROM %s\n", profile->rom); /* for a run by hand; the app shows only the line on stdout */
    switch (action) {
    case MAP: result = bm3_set_map(fd, profile, value); break;
    case BURBLES: result = bm3_set_burbles(fd, profile, value); break;
    case SOUND: result = bm3_set_sound(fd, value); break;
    default:
        active = bm3_read_slot(fd, profile);
        if (active < 0) puts("Could not read the active map");
        else printf("Map %d active\n", active + 1);
        result = active < 0;
    }
    release_gateway(&fd, &lock);
    return result;
}
#elif defined(MHD_MAPS_ONLY)
/* mhd-maps: MHD+ live controls, for the MHD+ Suite app. It prints one line for the app's status label.
 *   mhd-maps --host IPv4 --map 1..4      asks the DME for that map
 *   mhd-maps --host IPv4 --cooling 0|1   Max Cooling off or on; on only with the engine running
 * Map switching and Max Cooling aren't BMW jobs: MHD's code in the DME answers them. The commands are the ones
 * cauliflowermossgrape's Live Adjustments sends (apps/live_adjustments/tune.js), from a1-mapping's captures, and work
 * on an MG1 with an MHD+ multi-map tune:
 *     map         2F 01 <map - 1>   ->  6F 01 <map - 1>
 *     Max Cooling 2F 03 01 | 00     ->  6F 03 01 | 00
 * A request counts only when the DME echoes it. A DME without MHD refuses it. MHD's marker sits at a ROM-dependent
 * address and no read-back of the active map is known, so nothing is read first and the line says accepted, not
 * confirmed. Exit 0 when the DME accepted the request, 1 otherwise. */
enum { MHD_DME = 0x12, MHD_MAP = 0x01, MHD_COOLING = 0x03, MHD_TIMEOUT_MS = 3000 };
/* Live Adjustments' threshold for "running": over 500 rpm, from OBD's engine speed (01 0C, quarter rpm). */
enum { MHD_COOLING_RPM = 500 };

/* Sends 2F function value and waits for its echo: the exchange's result. */
static int mhd_send(int fd, unsigned function, unsigned value, unsigned *negative) {
    const unsigned char request[] = {0x2F, (unsigned char)function, (unsigned char)value};
    const unsigned char echo[] = {0x6F, (unsigned char)function, (unsigned char)value};
    unsigned char reply[16];
    return exchange(fd, MHD_DME, request, sizeof request, echo, sizeof echo, reply, sizeof reply, MHD_TIMEOUT_MS,
                    negative);
}

/* Engine speed in rpm, or -1 when the DME doesn't give it. */
static int mhd_engine_rpm(int fd) {
    const unsigned char request[] = {0x01, 0x0C}, prefix[] = {0x41, 0x0C};
    unsigned char reply[8];
    unsigned negative;
    int n = exchange(fd, MHD_DME, request, sizeof request, prefix, sizeof prefix, reply, sizeof reply, 1000, &negative);
    return n == 4 ? (int)((((unsigned)reply[2] << 8) | reply[3]) / 4) : -1;
}

static int mhd_result(int n, unsigned negative, const char *what, const char *accepted) {
    if (n > 0) {
        puts(accepted);
        return 0;
    }
    if (n == -2 && (negative == 0x11 || negative == 0x12 || negative == 0x13 || negative == 0x31))
        printf("The DME refused %s (NRC %02X). Is an MHD+ tune flashed?\n", what, negative);
    else if (n == -2) printf("The DME refused %s (NRC %02X)\n", what, negative);
    else printf("No answer from the DME for %s\n", what);
    return 1;
}

int main(int argc, char **argv) {
    enum { NONE, MAP, COOLING } action = NONE;
    const char *host_option = 0;
    char host[INET_ADDRSTRLEN] = "", what[32], accepted[64];
    unsigned port = 6801, value = 0, negative = 0;
    int fd, lock, i, n, result;
    uint64_t deadline;
    struct gateway_search search;
    gateway_search_init(&search);
    for (i = 1; i < argc; ++i) {
        int has_value = i + 1 < argc && action == NONE;
        if (!strcmp(argv[i], "--host") && i + 1 < argc && !host_option) host_option = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = number(argv[++i], 1, 65535);
        else if (!strcmp(argv[i], "--map") && has_value) { action = MAP; value = number(argv[++i], 1, 4); }
        else if (!strcmp(argv[i], "--cooling") && has_value) { action = COOLING; value = number(argv[++i], 0, 1); }
        else if (!gateway_search_option(&search, argc, argv, &i)) return 2;
    }
    if (host_option && strlen(host_option) < sizeof host) strcpy(host, host_option);
    if (!host[0] || action == NONE) {
        fprintf(stderr, "Usage: %s --host IPv4 [--port 6801] (--map 1-4 | --cooling 0-1)\n", argv[0]);
        return 2;
    }
    signal(SIGINT, stop); signal(SIGTERM, stop); signal(SIGPIPE, SIG_IGN);
    /* A gauge app still holding the gateway gives it up within a lease once asked (reserve_gateway asks). */
    lock = reserve_gateway();
    deadline = bd_mono_ms() + 650;
    while (running && lock < 0 && errno == EADDRINUSE && bd_mono_ms() < deadline) {
        poll(0, 0, 25);
        lock = reserve_gateway();
    }
    if (lock < 0) { puts("Diagnostics busy. Wait a moment and try again."); return 1; }
    fd = connect_gw(host, port);
    if (fd < 0) {
        search.failures = GATEWAY_SEARCH_FAILURES - 1; /* a one-shot run searches at once */
        if (gateway_search_after_failure(&search, host, port)) fd = connect_gw(host, port);
    }
    if (fd < 0) { puts("Diagnostic gateway unavailable"); close(lock); return 1; }
    if (action == MAP) {
        snprintf(what, sizeof what, "map %u", value);
        snprintf(accepted, sizeof accepted, "Map %u accepted by the DME", value);
        n = mhd_send(fd, MHD_MAP, value - 1, &negative);
        result = mhd_result(n, negative, what, accepted);
    } else if (value && mhd_engine_rpm(fd) <= MHD_COOLING_RPM) {
        puts("Start the engine first. Max Cooling needs it running.");
        result = 1;
    } else {
        n = mhd_send(fd, MHD_COOLING, value, &negative);
        result = mhd_result(n, negative, value ? "Max Cooling on" : "Max Cooling off",
                            value ? "Max Cooling on, accepted by the DME" : "Max Cooling off, accepted by the DME");
    }
    release_gateway(&fd, &lock);
    return result;
}
#elif defined(SHIFT_DECK_ONLY)
#include "drive_frame.h"

static int read_live_pid(int fd, unsigned char pid, int width, int *value) {
    unsigned char req[]={0x01,pid}, prefix[]={0x41,pid}, reply[4];
    unsigned negative;
    int n=exchange(fd,0x12,req,sizeof req,prefix,sizeof prefix,reply,sizeof reply,250,&negative);
    *value=-1;
    if (n==width+2) *value=width==1 ? reply[2] : ((unsigned)reply[2]*256+reply[3])/4;
    return n!=-1;
}

/* Engine speed on every pass, at most every DRIVE_CYCLE_MS, so the bar follows the revs; road speed changes slowly
 * and is read every DRIVE_SPEED_MS. */
enum { DRIVE_CYCLE_MS = 20, DRIVE_SPEED_MS = 65, DRIVE_IDLE_MS = 25 };

int main(int argc, char **argv) {
    const char *host_option=0;
    char host[INET_ADDRSTRLEN];
    unsigned port=6801, udp_port=DRIVE_PORT, cycles=0, count=0;
    pid_t parent=getppid();
    struct sockaddr_in dest;
    struct gateway_search search;
    int fd=-1, sock, lock=-1, i, last_kph=-1;
    uint64_t lease_until=0, retry_at=0, speed_due=0;
    gateway_search_init(&search);
    for (i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && i+1<argc && !host_option) host_option=argv[++i];
        else if (!strcmp(argv[i],"--port") && i+1<argc) port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--udp-port") && i+1<argc) udp_port=number(argv[++i],1,65535);
        else if (!strcmp(argv[i],"--cycles") && i+1<argc) cycles=number(argv[++i],1,1000);
        else if (!gateway_search_option(&search,argc,argv,&i)) return 2;
    }
    if (!host_option || strlen(host_option)>=sizeof host || parent<=1) return 2;
    strcpy(host,host_option);
    signal(SIGINT,stop); signal(SIGTERM,stop); signal(SIGPIPE,SIG_IGN);
    sock=socket(AF_INET,SOCK_DGRAM,0);
    if (sock<0) return 1;
    memset(&dest,0,sizeof dest);
    dest.sin_family=AF_INET;
    dest.sin_port=htons((uint16_t)udp_port);
    inet_pton(AF_INET,"127.0.0.1",&dest.sin_addr);
    while (running && getppid()==parent && (!cycles || count<cycles)) {
        struct drive_frame f;
        int rpm=-1,kph=-1;
        uint64_t start=bd_mono_ms(), elapsed;
        if (fd<0 && start>=retry_at) {
            lock=reserve_gateway();
            if (lock>=0) {
                fd=connect_gw(host,port);
                if (fd<0) {
                    release_gateway(&fd,&lock);
                    retry_at=bd_mono_ms()+DIAGNOSTIC_RETRY_MS;
                    if (gateway_search_after_failure(&search,host,port)) retry_at=0;
                } else {
                    lease_until=bd_mono_ms()+DIAGNOSTIC_LEASE_MS;
                    search.failures=0;
                    speed_due=0; /* a new connection reads the speed at once */
                }
            }
        }
        if (fd>=0) {
            int ok=read_live_pid(fd,0x0c,2,&rpm);
            if (ok && start>=speed_due) {
                ok=read_live_pid(fd,0x0d,1,&last_kph);
                speed_due=start+DRIVE_SPEED_MS;
            }
            kph=last_kph;
            if (!ok) {
                release_gateway(&fd,&lock);
                retry_at=bd_mono_ms()+DIAGNOSTIC_RETRY_MS;
            } else {
                f.sampled_ms=bd_mono_ms();
                f.rpm=rpm;
                f.kph=kph;
                if (sendto(sock,&f,sizeof f,0,(struct sockaddr *)&dest,sizeof dest)!=(ssize_t)sizeof f) break;
                if (gateway_should_yield(lock,f.sampled_ms,lease_until)) {
                    release_gateway(&fd,&lock);
                    retry_at=f.sampled_ms+DIAGNOSTIC_YIELD_MS;
                }
            }
        }
        ++count;
        elapsed=bd_mono_ms()-start;
        if (running) poll(0,0,fd<0 ? DRIVE_IDLE_MS : elapsed<DRIVE_CYCLE_MS ? (int)(DRIVE_CYCLE_MS-elapsed) : 2);
    }
    release_gateway(&fd,&lock);
    close(sock);
    return 0;
}
#elif defined(GATEWAY_TOOL)
/* ena-gateway: finds the diagnostic gateway from the head unit and checks the route to it, for setup.sh.
 *   ena-gateway discover [--dev NAME | --to IPv4] [--port 6811] [--wait MS]
 *     HSFZ vehicle identification: sends 00 00 00 00 00 11 to the broadcast address of every IPv4 network on the
 *     head unit's car-side Ethernet (vbr0, or --dev) and prints each responder from those networks that calls itself
 *     the gateway (DIAGADR10), one per line. Exit 0 if one answered, 1 if none.
 *     The gateway is usually on the link-local network (169.254.0.0/16). While a laptop on the ENET cable runs a DHCP
 *     server, the gateway and the head unit both take an address from it, and keep it for a while after the laptop
 *     goes: the gateway then answers only on that network. Seen on an F20 on 15 November 2026.
 *     --to sends to one address instead and accepts any responder, for tests.
 *   ena-gateway check --host IPv4 [--port 6801]
 *     Reads OBD 01 00 (the DME's supported PIDs) through the gateway. It only reads. Exit 0 on a positive reply. */
static int discover(const char *device, const char *to, unsigned port, unsigned wait_ms) {
    char found[MAX_GATEWAYS][INET_ADDRSTRLEN];
    int n = find_gateways(device, to, port, wait_ms, found, MAX_GATEWAYS), i;
    if (n < 0) return n == -1 ? 1 : 2;
    for (i = 0; i < n; ++i) puts(found[i]);
    return n ? 0 : 1;
}

static int check(const char *host, unsigned port) {
    const unsigned char request[] = {0x01, 0x00}, prefix[] = {0x41, 0x00};
    unsigned char reply[16];
    unsigned negative;
    int fd, lock, n;
    lock = reserve_gateway();
    if (lock < 0) { puts("Diagnostics busy. Close BoostDeck or ShiftDeck and try again."); return 1; }
    fd = connect_gw(host, port);
    if (fd < 0) {
        printf("No connection to %s port %u\n", host, port);
        close(lock);
        return 1;
    }
    n = exchange(fd, 0x12, request, sizeof request, prefix, sizeof prefix, reply, sizeof reply, 1500, &negative);
    release_gateway(&fd, &lock);
    if (n >= 2) { printf("Gateway %s answers; the DME replied to OBD 01 00\n", host); return 0; }
    if (n == -2) { printf("Gateway %s answers; the DME refused OBD 01 00 (NRC %02X)\n", host, negative); return 0; }
    printf("Connected to %s, but the DME did not answer\n", host);
    return 1;
}

int main(int argc, char **argv) {
    const char *host = 0, *to = 0, *device = "vbr0";
    unsigned port = 0, wait_ms = 1800;
    int discovering, i;
    if (argc < 2 || (strcmp(argv[1], "discover") && strcmp(argv[1], "check"))) {
        fprintf(stderr, "Usage: %s discover [--dev NAME | --to IPv4] [--port 6811] [--wait MS] | "
                        "check --host IPv4 [--port 6801]\n", argv[0]);
        return 2;
    }
    discovering = !strcmp(argv[1], "discover");
    for (i = 2; i < argc; ++i) {
        if (!discovering && !strcmp(argv[i], "--host") && i + 1 < argc && !host) host = argv[++i];
        else if (discovering && !strcmp(argv[i], "--to") && i + 1 < argc && !to) to = argv[++i];
        else if (discovering && !strcmp(argv[i], "--dev") && i + 1 < argc) device = argv[++i];
        else if (discovering && !strcmp(argv[i], "--wait") && i + 1 < argc) wait_ms = number(argv[++i], 100, 10000);
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = number(argv[++i], 1, 65535);
        else { fprintf(stderr, "Unknown argument: %s\n", argv[i]); return 2; }
    }
    signal(SIGINT, stop); signal(SIGTERM, stop); signal(SIGPIPE, SIG_IGN);
    if (discovering) return discover(device, to, port ? port : 6811, wait_ms);
    if (!host) { fprintf(stderr, "check needs --host\n"); return 2; }
    return check(host, port ? port : 6801);
}
#else
static int trace_reads = 0;
static void pause_ms(int ms) { while (running && poll(0, 0, ms) < 0 && errno == EINTR) {} }

static int read_did(int fd, unsigned ecu, unsigned did, double *value) {
    unsigned char request[]={0x22,(unsigned char)(did>>8),(unsigned char)did};
    unsigned char prefix[]={0x62,request[1],request[2]};
    unsigned char reply[4094];
    unsigned negative, i;
    int n=exchange(fd,ecu,request,3,prefix,3,reply,sizeof reply,500,&negative);
    int decoded;
    if (n==-2) {
        fprintf(stderr,"ECU %02X DID %04X NRC %02X\n",ecu,did,negative);
        return 0;
    }
    if (n<0) return -1;
    decoded=bd_decode_did(did,reply+3,(size_t)n-3,value);
    if (trace_reads) {
        fprintf(stderr,"RAW mono_ms=%llu ecu=%02X did=%04X payload=",
                (unsigned long long)bd_mono_ms(),ecu,did);
        for (i=3;i<(unsigned)n;++i) fprintf(stderr,"%02X",reply[i]);
        if (decoded) fprintf(stderr," decoded=%.6f\n",*value);
        else fprintf(stderr," unavailable%s\n",did==0x4a49 && n==5 &&
                     reply[3]==255 && reply[4]==255 ?
                     " reason=ambiguous-FFFF" : " reason=invalid-or-out-of-range");
    }
    return decoded;
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
    const char *host_option = 0;
    char host[INET_ADDRSTRLEN] = "";
    unsigned port = 6801, udp_port = BD_UDP_PORT, cycles = 0, count = 0, i;
    int demo = 0, sock, gw = -1, no_trans = 0, session = 0, option;
    int lock = -1;
    struct gateway_search search;
    pid_t parent = getppid();
    struct sockaddr_in dest;
    bd_frame f;
    uint64_t observed[BD_COUNT] = {0};
    double ambient = 0;
    uint64_t ambient_time = 0;
    int have_ambient = 0;
    /* A pass every 35 ms at most: the fast gauges are read as quickly as the gateway answers them. */
    const unsigned tick_ms = 35;
    const unsigned minimum_idle_ms = 3;
    /* Each slow signal has its own deadline. Stagger temperature reads
     * after the first scan so they do not delay the fast gauges together. */
    const unsigned dids[] = {0x4201,0x4205,0x5889,0x4a49,0x580f,0xda12,0x56d7,0x586f,0x4300};
    const unsigned periods[] = {1000,0,0,0,2000,2000,0,0,2000};
    const unsigned first_delay[] = {900,0,0,0,500,1150,0,0,1700};
    uint64_t due[sizeof dids/sizeof dids[0]] = {0};
    uint64_t failed_until[sizeof dids/sizeof dids[0]] = {0};
    uint64_t lease_until=0, retry_at=0;
    int scanned=0;
    gateway_search_init(&search);
    for (i = 1; i < (unsigned)argc; ++i) {
        option = (int)i;
        if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--session")) session = 1;
        else if (!strcmp(argv[i], "--trace")) trace_reads = 1;
        else if (!strcmp(argv[i], "--no-trans")) no_trans = 1;
        else if (gateway_search_option(&search, argc, argv, &option)) i = (unsigned)option;
        else if (!strcmp(argv[i], "--host") && i+1 < (unsigned)argc) host_option = argv[++i];
        else if (!strcmp(argv[i], "--port") && i+1 < (unsigned)argc) port = number(argv[++i],1,65535);
        else if (!strcmp(argv[i], "--udp-port") && i+1 < (unsigned)argc) udp_port = number(argv[++i],1,65535);
        else if (!strcmp(argv[i], "--cycles") && i+1 < (unsigned)argc) cycles = number(argv[++i],1,1000000);
        else { fprintf(stderr, "Usage: %s (--host IPv4 | --demo) [--port 6801] [--udp-port 42860] [--cycles N] [--no-trans] [--trace] [--session]\n", argv[0]); return 2; }
    }
    if ((!host_option && !demo) || (host_option && demo)) { fprintf(stderr, "Choose --host or --demo, never both.\n"); return 2; }
    if (host_option) {
        if (strlen(host_option) >= sizeof host) { fprintf(stderr, "Invalid host: %s\n", host_option); return 2; }
        strcpy(host, host_option);
    }
    signal(SIGINT, stop); signal(SIGTERM, stop); signal(SIGPIPE, SIG_IGN);
    if (session && (parent <= 1 || demo)) return 2;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("UDP socket"); return 1; }
    memset(&dest,0,sizeof dest); dest.sin_family=AF_INET; dest.sin_port=htons((uint16_t)udp_port);
    inet_pton(AF_INET,"127.0.0.1",&dest.sin_addr); /* local process IPC only */
    memset(&f,0,sizeof f); f.demo=(unsigned)demo;
    fprintf(stderr, "%s: 2016 340i candidate profile; validate against ISTA/logger.\n",
        demo ? "DEMO / NO VEHICLE CONNECTION" : "READ ONLY / HSFZ");
    while (running && (!cycles || count < cycles) && (!session || getppid() == parent)) {
        uint64_t start = bd_mono_ms();
        f.seq++;
        if (demo) {
            double t = count * (tick_ms / 1000.0);
            f.value[0]=12+8*sin(t); f.value[1]=13.2+.6*sin(t);
            f.value[2]=8+3*sin(t); f.value[3]=44+2*sin(t); f.value[4]=88;
            f.value[BD_RAIL]=95+22*sin(t); f.value[BD_OIL]=2.5+.5*sin(t);
            f.value[BD_COOLANT]=86+3*sin(t);
            f.valid=(1u<<BD_COUNT)-1;
            for (i=0;i<BD_COUNT;++i) observed[i]=bd_mono_ms();
        } else {
            int full_scan = session ? !scanned : gw < 0;
            if (gw < 0 && start >= retry_at) {
                if (session) lock=reserve_gateway();
                if (!session || lock>=0) {
                    gw=connect_gw(host,port);
                    if (gw<0 && session) {
                        release_gateway(&gw,&lock);
                        retry_at=bd_mono_ms()+DIAGNOSTIC_RETRY_MS;
                    } else if (session) lease_until=bd_mono_ms()+DIAGNOSTIC_LEASE_MS;
                    if (gw>=0) search.failures=0;
                    else if (gateway_search_after_failure(&search,host,port)) retry_at=0;
                }
            }
            for (i=0; gw >= 0 && i < sizeof dids / sizeof dids[0] && running; ++i) {
                double v = 0;
                int rc;
                if (no_trans && dids[i] == 0xda12) continue;
                if (session && start < failed_until[i]) continue;
                if (!full_scan && periods[i] && start < due[i]) continue;
                if (full_scan) due[i]=start+first_delay[i];
                else if (periods[i]) {
                    due[i]+=periods[i];
                    if (due[i]<=start) due[i]=start+periods[i];
                }
                if (i != 0) f.valid &= ~(1u << (i-1));
                rc = read_did(gw, dids[i] == 0xda12 ? 0x18 : 0x12, dids[i], &v);
                if (rc < 0) {
                    release_gateway(&gw,&lock);
                    if (session) {
                        failed_until[i]=bd_mono_ms()+2000;
                        retry_at=bd_mono_ms()+DIAGNOSTIC_RETRY_MS;
                    } else {
                        f.valid=0;
                        have_ambient=0;
                    }
                    break;
                }
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
                f.value[i-1]=v; observed[i-1]=bd_mono_ms(); f.valid |= 1u << (i-1);
            }
            if (gw>=0) {
                scanned=1;
                if (session && gateway_should_yield(lock,bd_mono_ms(),lease_until)) {
                    release_gateway(&gw,&lock);
                    retry_at=bd_mono_ms()+DIAGNOSTIC_YIELD_MS;
                }
            }
        }
        if (!send_frame(sock,&dest,&f,observed)) { perror("send frame"); running=0; }
        ++count;
        {
            uint64_t elapsed = bd_mono_ms()-start;
            unsigned interval = session && gw<0 ? 55 : tick_ms;
            unsigned idle_ms = elapsed < interval ? (unsigned)(interval-elapsed) : 0;
            if (!demo && idle_ms < minimum_idle_ms) idle_ms = minimum_idle_ms;
            if (idle_ms) pause_ms((int)idle_ms);
        }
    }
    release_gateway(&gw,&lock);
    close(sock);
    return 0;
}
#endif
