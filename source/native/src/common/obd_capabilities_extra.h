#ifndef OBD_CAPABILITIES_EXTRA_H
#define OBD_CAPABILITIES_EXTRA_H
#include "obd_capabilities.h"

/* Explicit higher-page callers only. The shared four-page policy is unchanged.
 * Keep this cache with the same ECU/endpoint and worker lifetime as standard. */
struct obd_extra_capabilities { uint32_t bits; uint64_t retry_at; unsigned known; };
#ifdef __cplusplus
extern "C" {
#endif
/* Only bitmap 80 is added; measurement PIDs must be in 81..9F. A newly read
 * bitmap returns PENDING, so it cannot also trigger a measurement this call. */
int obd_pid_advertised_extra(struct obd_capabilities *standard, struct obd_extra_capabilities *,
                             struct diag_link *, unsigned pid);
#ifdef __cplusplus
}
#endif
#endif
