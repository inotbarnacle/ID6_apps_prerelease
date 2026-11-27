#ifndef DPF_CORE_H
#define DPF_CORE_H

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* These IDs belong only to DPFDeck. DPF1 always has exactly eleven fields. */
enum { DPF_B1_DELTA, DPF_B1_INLET_PRESSURE, DPF_B1_OUTLET_PRESSURE,
       DPF_B2_DELTA, DPF_B2_INLET_PRESSURE, DPF_B2_OUTLET_PRESSURE,
       DPF_B1_INLET_TEMP, DPF_B1_OUTLET_TEMP, DPF_B2_INLET_TEMP, DPF_B2_OUTLET_TEMP,
       DPF_COOLANT, DPF_COUNT };
enum { DPF_PORT=42867, DPF_MAX_PACKET=512, DPF_STALE_MS=4500,
       DPF_PERIOD_MS=2000, DPF_RETRY_MS=5000, DPF_TEMP_MAX_C=1800 };
#define DPF_BIT(i) (UINT32_C(1) << (i))
#define DPF_ALL (DPF_BIT(DPF_COUNT)-1)
enum { DPF_UNKNOWN, DPF_UNSUPPORTED, DPF_UNAVAILABLE, DPF_STALE, DPF_VALID };

struct dpf_sample { unsigned known, supported, valid; double value[DPF_COUNT]; };
struct dpf_frame {
    uint32_t seq, worker_pid;
    uint64_t token, sent_ms;
    unsigned known, supported, valid;
    double value[DPF_COUNT]; /* kPa and C; inlet/outlet pressure reference unestablished */
    unsigned age_ms[DPF_COUNT];
};

/* Data after 41 <pid>. A malformed layout leaves *out unchanged. Valid but
 * unsupported fields have no value. Temperature above 1800 C fails this app's
 * plausibility envelope; this is not a BMW operating or regeneration limit. */
unsigned dpf_pid_mask(unsigned pid);
int dpf_decode(unsigned pid, const unsigned char *, size_t, struct dpf_sample *out);
int dpf_encode(const struct dpf_frame *, char *, size_t);
int dpf_parse(const char *, size_t, struct dpf_frame *out);
int dpf_measurement_state(const struct dpf_frame *, unsigned field, uint64_t elapsed_ms);
double dpf_display_value(unsigned field, double value, int metric);
const char *dpf_unit(unsigned field, int metric);

#ifdef __cplusplus
}
#endif
#endif
