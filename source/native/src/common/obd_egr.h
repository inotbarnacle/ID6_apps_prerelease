#ifndef OBD_EGR_H
#define OBD_EGR_H

#include <stddef.h>
#include <string.h>

enum { OBD_EGR_FIELDS=6, OBD_EGR_BYTES=7, OBD_EGR_RETRY_MS=5000 };
struct obd_egr_sample { unsigned present; double value[OBD_EGR_FIELDS]; };

/* Mode 01 PID 69 data: support bits 0..5, then A command/actual/error,
 * B command/actual/error. Command and actual are normalized duty/position,
 * not mass flow. Error is a separate signed percentage, never actual EGR.
 * Format provenance and validation limits: docs/STANDARD-EGR.md. */
static inline int obd_egr_decode(const unsigned char* data, size_t n, struct obd_egr_sample* out) {
    if (!out) return 0;
    memset(out,0,sizeof *out);
    if (!data || n!=OBD_EGR_BYTES || (data[0]&0xc0)) return 0;
    out->present=data[0];
    for (unsigned i=0;i<OBD_EGR_FIELDS;++i) {
        if (!(out->present & (1u<<i))) continue;
        out->value[i]=i%3==2 ? ((int)data[1+i]-128)*(100.0/128) : data[1+i]*100.0/255;
    }
    return 1;
}

#endif
