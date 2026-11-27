#ifndef DPF_REGEN_CAPABILITIES_H
#define DPF_REGEN_CAPABILITIES_H
#include "obd_capabilities.h"

/* Optional PID 8B only. The shared four-page cache and its default policy are
 * unchanged. Keep this extension with the same ECU/endpoint/worker lifetime. */
struct dpf_regen_capabilities { uint32_t bits; uint64_t retry_at; unsigned known; };
static inline unsigned dpf_capability_generation(const struct obd_capabilities *standard,
                                                const struct dpf_regen_capabilities *regen) {
    return standard->known | (regen->known ? 16u : 0u);
}
int dpf_regen_advertised(struct obd_capabilities *, struct dpf_regen_capabilities *, struct diag_link *);
#endif
