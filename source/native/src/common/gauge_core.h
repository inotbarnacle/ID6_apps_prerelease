#ifndef BOOSTDECK_CORE_H
#define BOOSTDECK_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
#define SIGNAL(id, name, lo, hi, unit, wide, narrow) BD_##id,
#include "boost_signals.def"
#undef SIGNAL
    BD_COUNT
};
typedef char bd_channels_fit_mask[BD_COUNT < 32 ? 1 : -1];
const char *bd_name(unsigned i);
enum { BD_MAX_PACKET = 768, BD_STALE_MS = 2500, BD_TEMP_STALE_MS = 3500,
       BD_UDP_PORT = 42860 };
typedef struct {
    uint32_t seq;
    unsigned demo, valid;
    uint64_t sent_ms; /* sender CLOCK_MONOTONIC, shared by processes on the same QNX node */
    double value[BD_COUNT]; /* psi, gasoline-equivalent AFR, degrees, C, C, bar, bar, C
                               extended channels: ethanol %, oil temperature C, battery V */
    unsigned age_ms[BD_COUNT];
} bd_frame;
uint64_t bd_mono_ms(void);
int bd_decode_did(unsigned did, const unsigned char *data, size_t len, double *out);
/* Standard OBD-II mode 01 PIDs (SAE J1979): data bytes after the "41 <pid>" prefix. */
/* GS7D36SG (M DCT) live status block: EGS 22 DE31, 38 payload bytes of big-endian words.
   The DCT rejects the ZF DA12/DA2E reads with NRC 31. */
enum { BD_DKG_STATUS_SIZE = 38 };
int bd_decode_dkg_oil(const unsigned char *data, size_t len, double *out);
int bd_dkg_parked(const unsigned char *data, size_t len);
int bd_decode_obd(unsigned pid, const unsigned char *data, size_t len, double *out);
int bd_encode(const bd_frame *f, char *out, size_t len);
int bd_parse(const char *data, size_t len, bd_frame *out);
int bd_is_fresh(const bd_frame *f, unsigned i, uint64_t elapsed_ms);
int bd_is_pressure(unsigned i);
double bd_display_selected(unsigned i, double value, unsigned selection);
const char *bd_unit_selected(unsigned i, unsigned selection);
double bd_display_value(unsigned i, double value, int metric);
int bd_bar(unsigned i, double value); /* normalized 0..1000, independent of units */
const char *bd_unit(unsigned i, int metric);
#ifdef __cplusplus
}
#endif
#endif
