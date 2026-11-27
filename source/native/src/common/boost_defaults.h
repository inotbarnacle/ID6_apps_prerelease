#ifndef BOOST_DEFAULTS_H
#define BOOST_DEFAULTS_H
#include "gauge_core.h"
#include "obd_capabilities.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { BD_DEFAULT_SLOTS=9, BD_EMPTY_SLOT=255, BD_CAP_PACKET=160,
       BD_AUTO_PACKET=BD_MAX_PACKET+BD_CAP_PACKET };
typedef struct {
    uint64_t token, sent_ms;
    uint32_t worker_pid, known, supported;
} bd_cap_frame;
unsigned bd_diesel_candidates(void);
void bd_diesel_masks(const struct obd_capabilities *, int trans_observed, unsigned *known, unsigned *supported);
void bd_diesel_slots(unsigned supported, unsigned char slots[BD_DEFAULT_SLOTS]);
unsigned bd_slots_mask(const unsigned char slots[BD_DEFAULT_SLOTS]);
int bd_cap_encode(const bd_cap_frame *, char *, size_t);
int bd_cap_parse(const char *, size_t, bd_cap_frame *);
/* Automatic mode wraps the unchanged NBG7 frame in a capability envelope.
 * Custom/gasoline workers keep sending ordinary NBG7 packets. */
int bd_auto_encode(const bd_cap_frame *, const bd_frame *, char *, size_t);
int bd_auto_parse(const char *, size_t, bd_cap_frame *, bd_frame *);
#ifdef __cplusplus
}
#endif
#endif
