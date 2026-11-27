#define _POSIX_C_SOURCE 200809L
#include "diag_link.h"
#include "diagnostic_lease.h"
#include "gauge_core.h"
#include "gauge_coexistence.h"
#include "most/dsi_client.h"
#include "most/most_link.h"
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

/* A DSI send has no timeout, so once MOST is in use SIGALRM's default action bounds every loop and the
 * shutdown. */
enum { LOOP_FAILS_TO_MOST = 5, MOST_OPEN_MS = 2000, MOST_RETRY_MS = 30000, MOST_GAUGE_RETRY_MS = 250,
       MOST_WAIT_MIN_MS = 2000,
       LOOP_WATCHDOG_S = 60, STOP_WATCHDOG_S = 15, LOOP_LEASE_WAIT_MS = 300, ONESHOT_LEASE_WAIT_MS = 3000,
       LEASE_POLL_MS = 20, MOST_YIELD_MS = 100 };
static volatile sig_atomic_t most_armed;

static void stop(int sig) {
    (void)sig;
    if (diag_running && most_armed) alarm(STOP_WATCHDOG_S); /* first signal only */
    diag_running = 0;
}

void diag_link_signals(void) {
    struct sigaction sa;
    sigset_t alarm_set;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = stop;
    sigaction(SIGINT, &sa, 0); sigaction(SIGTERM, &sa, 0);
    signal(SIGPIPE, SIG_IGN); signal(SIGALRM, SIG_DFL);
    sigemptyset(&alarm_set); sigaddset(&alarm_set, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &alarm_set, 0);
}

void diag_link_init(struct diag_link *l, const struct diag_link_cfg *cfg) {
    memset(l, 0, sizeof *l);
    l->cfg = *cfg;
    if (l->cfg.transport == DIAG_MOST) l->cfg.host = 0;
    l->fd = l->lock = l->gauge_most_lock = -1;
    l->most_usable = 1;
}

static void pause_until(uint64_t until) {
    uint64_t now;
    while (diag_running && (now = bd_mono_ms()) < until) poll(0, 0, (int)(until - now));
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

/* 1 when up, -1 while a gauge/HUD owns arbitration. A failed open is closed here; when that close cannot stop the DSI thread, or the
 * library is missing, MOST stays off for this process. */
static int open_most(struct diag_link *l) {
    enum most_open_rc rc;
    unsigned gauge_port=l->cfg.kind==DIAG_LOOP ? gauge_most_port(l->cfg.dsi_name) : 0;
    if (gauge_port) {
        int arbitration;
        l->gauge_most_lock=gauge_bind(gauge_port);
        if (l->gauge_most_lock<0) return -1;
        /* The marker closes the race with a new HUD worker after arbitration is released. */
        arbitration=gauge_bind(GAUGE_MOST_PORT);
        if (arbitration<0) {
            if (errno==EADDRINUSE) gauge_request_most();
            close(l->gauge_most_lock);
            l->gauge_most_lock=-1;
            return -1;
        }
        close(arbitration);
    }
    most_armed = 1;
    alarm(LOOP_WATCHDOG_S);
    if (l->cfg.dsi_name) dsi_client_set_name(l->cfg.dsi_name);
    most_set_running(&diag_running);
    rc = most_open(MOST_OPEN_MS);
    if (rc == MOST_UP) { fputs("transport: MOST\n", stderr); return 1; }
    fprintf(stderr, "MOST open failed: %s\n", rc == MOST_NO_LIBRARY ? "no library" :
            rc == MOST_NO_TARGET ? "no target" : "no server");
    if (rc == MOST_NO_LIBRARY) l->most_usable = 0;
    alarm(STOP_WATCHDOG_S);
    if (most_close() != 0) l->most_usable = 0;
    else if (l->gauge_most_lock>=0) {
        close(l->gauge_most_lock);
        l->gauge_most_lock=-1;
    }
    if (diag_running) alarm(0);
    return 0;
}

int diag_link_up(struct diag_link *l) {
    int oneshot = l->cfg.kind == DIAG_ONESHOT, session = l->cfg.session && !oneshot;
    uint64_t now = bd_mono_ms();
    int fallback_due=l->cfg.most_after_hsfz && l->cfg.transport!=DIAG_HSFZ && l->most_usable &&
        l->hsfz_fails>=(oneshot ? 1u : (unsigned)LOOP_FAILS_TO_MOST) && now>=l->most_retry_at;
    if (l->on_most) return !l->lost;
    if (l->fd >= 0) return 1;
    if (l->transaction) return 0;
    if (l->cfg.host && now >= l->retry_at && !fallback_due) {
        if (session) l->lock = reserve_gateway();
        if (!session || l->lock >= 0) {
            l->fd = diag_hsfz_connect(l->cfg.host, l->cfg.port);
            if (l->fd<0) ++l->hsfz_fails;
            else if (!l->cfg.most_after_hsfz) l->hsfz_fails=0;
            if (l->fd >= 0) l->hsfz_seen = 1;
            if (l->fd < 0 && session) {
                release_gateway(&l->fd, &l->lock);
                l->retry_at = bd_mono_ms()+DIAGNOSTIC_RETRY_MS;
            } else if (session) l->lease_until = bd_mono_ms()+DIAGNOSTIC_LEASE_MS;
        }
    }
    if (l->fd >= 0) return 1;
    /* Only opted-in live profiles may fall back after an earlier HSFZ connection. */
    if (l->most_usable && (!l->hsfz_seen || l->cfg.most_after_hsfz) &&
        l->cfg.transport != DIAG_HSFZ && diag_running &&
        now >= l->most_retry_at &&
        (!l->cfg.host || l->hsfz_fails >= (oneshot ? 1u : (unsigned)LOOP_FAILS_TO_MOST))) {
        int opened=open_most(l);
        if (opened==1) { l->on_most=1;return 1; }
        if (opened<0) {
            l->most_retry_at=bd_mono_ms()+MOST_GAUGE_RETRY_MS;
            return 0;
        }
        if (oneshot) l->most_usable = 0;
        l->most_retry_at = bd_mono_ms()+MOST_RETRY_MS;
        l->hsfz_fails = 0;
    }
    return 0;
}

static void release_most_lease(struct diag_link *l,int lock) {
    char waiting;
    if (lock<0) return;
    if (recv(lock,&waiting,sizeof waiting,0)>0) l->most_yield_until=bd_mono_ms()+MOST_YIELD_MS;
    close(lock);
}

int diag_link_begin(struct diag_link *l) {
    if (!diag_running || l->transaction || (l->on_most ? l->lost : l->fd<0)) return 0;
    if (l->on_most && l->cfg.kind==DIAG_LOOP) {
        pause_until(l->most_yield_until);
        if (!diag_running || (l->lock=take_lease(LOOP_LEASE_WAIT_MS))<0) return 0;
    } else if (l->on_most && l->lock<0) return 0;
    l->transaction=1;
    l->transaction_io_failed=0;
    return 1;
}

void diag_link_end(struct diag_link *l,int complete) {
    if (!l->transaction) return;
    l->transaction=0;
    if (l->on_most && l->cfg.kind==DIAG_LOOP && !l->lost) {
        release_most_lease(l,l->lock);
        l->lock=-1;
    } else if (!l->on_most && l->cfg.most_after_hsfz) {
        if (l->transaction_io_failed) {
            if (l->hsfz_fails<LOOP_FAILS_TO_MOST) ++l->hsfz_fails;
        } else if (complete) l->hsfz_fails=0;
    }
    l->transaction_io_failed=0;
}

/* Only DME 0x12 has been seen through the tunnel. A loop worker normally leases each request; a
 * multi-request sequence or one-shot keeps its existing lease until end or shutdown. */
static int most_request_link(struct diag_link *l, unsigned ecu, const unsigned char *uds, unsigned n,
                             const unsigned char *prefix, unsigned pn, unsigned char *reply, unsigned cap,
                             unsigned timeout_ms, unsigned *negative) {
    size_t len = 0;
    enum most_rc rc;
    int lock = -1;
    if (l->lost) return -1;
    if (!diag_running || ecu != MOST_ECU_DME || n == 0 || n > 16) return -3;
    if (l->cfg.kind == DIAG_LOOP && !l->transaction) {
        pause_until(l->most_yield_until);   /* a yield does not count against the 300 ms */
        if ((lock = take_lease(LOOP_LEASE_WAIT_MS)) < 0) return -3;
    }
    rc = most_request_wait(MOST_ECU_DME, uds, n, reply, cap, &len,
                           timeout_ms > MOST_WAIT_MIN_MS ? timeout_ms : MOST_WAIT_MIN_MS);
    /* F303 replies do not identify their definition. A late reply must never become a new sample. */
    if (rc == MOST_CLOSED || (rc == MOST_TIMEOUT &&
        ((l->transaction && l->cfg.most_after_hsfz) ||
         (n >= 3 && uds[0] == 0x22 && uds[1] == 0xf3 && uds[2] == 3) ||
         (n >= 4 && uds[0] == 0x2c && uds[2] == 0xf3 && uds[3] == 3)))) {
        l->lost = 1;
        if (lock >= 0) l->lock = lock; /* Retain ownership through DSI unregister/stop. */
        return -1;
    }
    release_most_lease(l,lock);
    if (rc != MOST_REPLY) return -3;
    if (len >= 3 && reply[0] == 0x7f) { *negative = reply[2]; return -2; }
    if (len < pn || memcmp(reply, prefix, pn)) return -3;
    return (int)len;
}

int diag_link_request(struct diag_link *l, unsigned ecu, const unsigned char *uds, unsigned n,
                      const unsigned char *prefix, unsigned pn, unsigned char *reply, unsigned cap,
                      unsigned timeout_ms, unsigned *negative) {
    int rc;
    *negative = 0;
    if (l->on_most) return most_request_link(l, ecu, uds, n, prefix, pn, reply, cap, timeout_ms, negative);
    if (l->fd < 0) return -1;
    rc=diag_hsfz_exchange(l->fd, ecu, uds, n, prefix, pn, reply, cap, timeout_ms, negative);
    if (rc==-1 && l->cfg.most_after_hsfz) {
        if (l->transaction) l->transaction_io_failed=1;
        else if (l->hsfz_fails<LOOP_FAILS_TO_MOST) ++l->hsfz_fails;
    }
    return rc;
}

/* A one-shot worker keeps its lease for the run. */
void diag_link_drop(struct diag_link *l) {
    if (l->on_most) return;
    if (l->cfg.kind == DIAG_ONESHOT) {
        if (l->fd >= 0) close(l->fd);
        l->fd = -1;
        return;
    }
    release_gateway(&l->fd, &l->lock);
    if (l->cfg.session) l->retry_at = bd_mono_ms()+DIAGNOSTIC_RETRY_MS;
}

int diag_link_on_most(const struct diag_link *l) { return l->on_most; }
int diag_link_lost(const struct diag_link *l) { return l->on_most && l->lost; }

int diag_link_should_yield(struct diag_link *l, uint64_t now) {
    if (l->on_most || l->fd < 0 || !l->cfg.session || l->cfg.kind != DIAG_LOOP ||
        !gateway_should_yield(l->lock, now, l->lease_until)) return 0;
    release_gateway(&l->fd, &l->lock);
    l->retry_at = now+DIAGNOSTIC_YIELD_MS;
    return 1;
}

void diag_link_tick(struct diag_link *l) {
    if (l->on_most) alarm(diag_running ? LOOP_WATCHDOG_S : STOP_WATCHDOG_S);
}

void diag_link_close(struct diag_link *l) {
    int closed = 1;
    l->transaction=0;
    l->transaction_io_failed=0;
    if (most_armed) {
        alarm(STOP_WATCHDOG_S);
        closed = most_close() == 0;
        if (closed) {
            if (l->gauge_most_lock>=0) close(l->gauge_most_lock);
            l->gauge_most_lock=-1;
            if (diag_running) alarm(0);
        }
    }
    if (closed) release_gateway(&l->fd, &l->lock);
    else if (l->fd >= 0) { close(l->fd); l->fd = -1; }
    l->on_most = 0;
}
