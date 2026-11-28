/* One HSFZ diagnostic link per worker, used from one thread. */
#ifndef DIAG_LINK_H
#define DIAG_LINK_H

#include <signal.h>
#include <stdint.h>

enum { DIAG_LOOP, DIAG_ONESHOT };
struct diag_link_cfg { const char *host; unsigned port; int kind; };
struct diag_link {
    struct diag_link_cfg cfg;
    int fd, lock;
    uint64_t retry_at, lease_until;
};

extern volatile sig_atomic_t diag_running;          /* cleared by SIGINT and SIGTERM */
void diag_link_init(struct diag_link *, const struct diag_link_cfg *);
/* SIGINT and SIGTERM stop the link. SIGPIPE is ignored. */
void diag_link_signals(void);
/* Connect or retry the HSFZ socket. Returns 1 when connected. */
int  diag_link_up(struct diag_link *);
/* UDS reply length, -1 for a lost link, or -2 for a negative reply with its code in *negative. */
int  diag_link_request(struct diag_link *, unsigned ecu, const unsigned char *uds, unsigned n,
                       const unsigned char *prefix, unsigned pn, unsigned char *reply, unsigned cap,
                       unsigned timeout_ms, unsigned *negative);
void diag_link_drop(struct diag_link *);
/* Returns 1 after releasing the lease to a waiting worker and scheduling a retry. */
int  diag_link_should_yield(struct diag_link *, uint64_t now);
void diag_link_close(struct diag_link *);
/* Direct HSFZ connection and exchange for workers that manage their own lease. */
int  diag_hsfz_connect(const char *host, unsigned port);
int  diag_hsfz_exchange(int fd, unsigned ecu, const unsigned char *uds, unsigned n,
                        const unsigned char *prefix, unsigned pn, unsigned char *reply, unsigned cap,
                        unsigned timeout_ms, unsigned *negative);
/* One-shot lease for the run. Returns 0 after a failed attempt of up to three seconds. */
int  diag_link_reserve_oneshot(struct diag_link *);

#endif
