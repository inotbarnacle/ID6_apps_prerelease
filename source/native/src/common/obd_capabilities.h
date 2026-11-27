#ifndef OBD_CAPABILITIES_H
#define OBD_CAPABILITIES_H

#include <stdint.h>

struct diag_link;
enum { OBD_CAPABILITY_PAGES = 3, OBD_CAPABILITY_RETRY_MS = 5000, OBD_CAPABILITY_PENDING = 2 };
/* One DME and endpoint per worker lifetime. Zero-initialize on worker startup.
 * Retain across short HSFZ leases; never share between vehicles or ECUs. */
struct obd_capabilities {
    uint32_t bits[OBD_CAPABILITY_PAGES];
    uint64_t retry_at[OBD_CAPABILITY_PAGES];
    unsigned known;
};

/* 1 advertised; 0 unsupported; OBD_CAPABILITY_PENDING not yet established; -1 link lost;
 * -3 MOST request skipped/failed. Sends at most one Mode 01 bitmap read per
 * call. Follows continuation bits and accepts only exact six-byte replies.
 * A caller must still validate each measurement; advertisement is not data. */
int obd_pid_advertised(struct obd_capabilities *, struct diag_link *, unsigned pid);

#endif
