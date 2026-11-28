#define _POSIX_C_SOURCE 200809L
#include "diag_link.h"
#include "diagnostic_lease.h"
#include "gauge_core.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

volatile sig_atomic_t diag_running = 1;

static int ready(int fd, short events, uint64_t deadline) {
    struct pollfd p;
    int rc;
    p.fd = fd; p.events = events; p.revents = 0;
    while (diag_running) {
        uint64_t now = bd_mono_ms();
        if (now >= deadline) return 0;
        rc = poll(&p, 1, (int)(deadline - now));
        if (rc < 0 && errno == EINTR) continue;
        return rc > 0 && (p.revents & events);
    }
    return 0;
}
static int transfer(int fd, unsigned char *buf, size_t n, int sending, uint64_t deadline) {
    while (n && diag_running) {
        ssize_t k;
        if (!ready(fd, sending ? POLLOUT : POLLIN, deadline)) return 0;
        k = sending ? send(fd, buf, n, 0) : recv(fd, buf, n, 0);
        if (k < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (k <= 0) return 0;
        buf += k; n -= (size_t)k;
    }
    return n == 0;
}
static int connect_ipv4(const char *host, unsigned port) {
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

/* HSFZ UDP identification on the wired link-local subnet. Require a single
 * diagnostic gateway (DIAGADR10); head-unit replies have different tags. */
static int discover_gateway(char host[16]) {
    const unsigned char query[] = {0,0,0,0,0,0x11};
    const char tag[] = "DIAGADR10";
    int fd=socket(AF_INET, SOCK_DGRAM, 0);
    int enable=1, matches=0;
    struct sockaddr_in dest;
    struct in_addr found;
    if (fd<0) return 0;
    memset(&dest,0,sizeof dest);
    dest.sin_family=AF_INET;
    dest.sin_addr.s_addr=htonl(0xa9feffffu);
    dest.sin_port=htons(6811);
    if (setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&enable,sizeof enable)==0 &&
        fcntl(fd,F_SETFL,O_NONBLOCK)==0 &&
        sendto(fd,query,sizeof query,0,(struct sockaddr*)&dest,sizeof dest)==(ssize_t)sizeof query) {
        uint64_t until=bd_mono_ms()+600;
        while (ready(fd,POLLIN,until)) {
            unsigned char reply[1024];
            struct sockaddr_in sender;
            socklen_t size=sizeof sender;
            ssize_t count=recvfrom(fd,reply,sizeof reply,0,(struct sockaddr*)&sender,&size);
            if (count<6 || size<sizeof sender || sender.sin_family!=AF_INET) continue;
            uint32_t ip=ntohl(sender.sin_addr.s_addr);
            if ((ip & 0xffff0000u)!=0xa9fe0000u || (ip & 0x0000ff00u)==0 ||
                (ip & 0x0000ff00u)==0x0000ff00u || reply[4]!=0 || reply[5]!=0x11) continue;
            for (ssize_t i=6;i+(ssize_t)sizeof tag<=count;++i) {
                if (memcmp(reply+i,tag,sizeof tag-1) || (reply[i+sizeof tag-1]>='0' &&
                    reply[i+sizeof tag-1]<='9')) continue;
                if (matches && found.s_addr!=sender.sin_addr.s_addr) {
                    close(fd);
                    return 0;
                }
                found=sender.sin_addr;
                matches=1;
                break;
            }
        }
    }
    close(fd);
    return matches && inet_ntop(AF_INET,&found,host,16)!=0;
}

int diag_hsfz_connect(const char *host, unsigned port) {
    if (strcmp(host,"auto")) return connect_ipv4(host,port);
    char candidate[16];
    /* Preserve the established address when it answers, and try discovery only
     * on the default vehicle port when the established endpoint is unreachable. */
    int fd=connect_ipv4("169.254.21.3",port);
    if (fd>=0 || port!=6801 || !discover_gateway(candidate)) return fd;
    return connect_ipv4(candidate,port);
}

/* Callers supply the UDS service, target, and response prefix. */
int diag_hsfz_exchange(int fd, unsigned ecu, const unsigned char *uds, unsigned uds_size,
                       const unsigned char *prefix, unsigned prefix_size,
                       unsigned char *reply, unsigned reply_size, unsigned timeout_ms,
                       unsigned *negative) {
    unsigned char request[48] = {0,0,0,0,0,1,0xf4,0};
    unsigned char header[6], body[4096];
    uint64_t deadline = bd_mono_ms() + timeout_ms;
    if (uds_size > sizeof request - 8) return -1;
    request[3] = (unsigned char)(uds_size+2);
    request[7] = (unsigned char)ecu;
    memcpy(request+8,uds,uds_size);
    *negative=0;
    if (!transfer(fd,request,8+uds_size,1,deadline)) return -1;
    while (diag_running) {
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

enum { ONESHOT_LEASE_WAIT_MS = 3000, LEASE_POLL_MS = 20 };

static void stop(int signal_number) {
    (void)signal_number;
    diag_running = 0;
}

void diag_link_signals(void) {
    struct sigaction action;
    memset(&action,0,sizeof action);
    action.sa_handler = stop;
    sigaction(SIGINT,&action,0);
    sigaction(SIGTERM,&action,0);
    signal(SIGPIPE,SIG_IGN);
}

void diag_link_init(struct diag_link *link,const struct diag_link_cfg *config) {
    memset(link,0,sizeof *link);
    link->cfg = *config;
    link->fd = link->lock = -1;
}

/* The lease, retrying while another worker holds it. errno is the last try's. */
static int take_lease(unsigned wait_ms) {
    uint64_t until = bd_mono_ms() + wait_ms;
    for (;;) {
        int lock = reserve_gateway();
        uint64_t now;
        if (lock >= 0 || errno != EADDRINUSE || !diag_running) return lock;
        now = bd_mono_ms();
        if (now >= until) return lock;
        poll(0, 0, until - now < LEASE_POLL_MS ? (int)(until - now) : LEASE_POLL_MS);
    }
}

int diag_link_reserve_oneshot(struct diag_link *l) {
    if (l->lock < 0) l->lock = take_lease(ONESHOT_LEASE_WAIT_MS);
    return l->lock >= 0;
}

int diag_link_up(struct diag_link *l) {
    int session = l->cfg.kind == DIAG_LOOP;
    uint64_t now = bd_mono_ms();
    if (l->fd >= 0) return 1;
    if (l->cfg.host && now >= l->retry_at) {
        if (session) l->lock = reserve_gateway();
        if (!session || l->lock >= 0) {
            l->fd = diag_hsfz_connect(l->cfg.host, l->cfg.port);
            if (l->fd < 0 && session) {
                release_gateway(&l->fd, &l->lock);
                l->retry_at = bd_mono_ms()+DIAGNOSTIC_RETRY_MS;
            } else if (session) l->lease_until = bd_mono_ms()+DIAGNOSTIC_LEASE_MS;
        }
    }
    if (l->fd >= 0) return 1;
    return 0;
}

int diag_link_request(struct diag_link *l,unsigned ecu,const unsigned char *uds,unsigned n,
                      const unsigned char *prefix,unsigned pn,unsigned char *reply,unsigned cap,
                      unsigned timeout_ms,unsigned *negative) {
    *negative = 0;
    if (l->fd < 0) return -1;
    return diag_hsfz_exchange(l->fd,ecu,uds,n,prefix,pn,reply,cap,timeout_ms,negative);
}

/* A one-shot worker keeps its lease for the run. */
void diag_link_drop(struct diag_link *l) {
    if (l->cfg.kind == DIAG_ONESHOT) {
        if (l->fd >= 0) close(l->fd);
        l->fd = -1;
        return;
    }
    release_gateway(&l->fd, &l->lock);
    l->retry_at = bd_mono_ms()+DIAGNOSTIC_RETRY_MS;
}

int diag_link_should_yield(struct diag_link *l, uint64_t now) {
    if (l->fd < 0 || l->cfg.kind != DIAG_LOOP ||
        !gateway_should_yield(l->lock, now, l->lease_until)) return 0;
    release_gateway(&l->fd, &l->lock);
    l->retry_at = now+DIAGNOSTIC_YIELD_MS;
    return 1;
}

void diag_link_close(struct diag_link *l) {
    release_gateway(&l->fd,&l->lock);
}
