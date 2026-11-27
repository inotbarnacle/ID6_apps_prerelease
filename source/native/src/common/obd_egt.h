#ifndef OBD_EGT_H
#define OBD_EGT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum { OBD_EGT_BANKS=2, OBD_EGT_SENSORS=4, OBD_EGT_BYTES=9, OBD_EGT_RETRY_MS=5000 };
struct obd_egt_sample { unsigned present, valid; double celsius[OBD_EGT_SENSORS]; };
/* Support evidence is cached for this worker/endpoint, like PID bitmaps and EGS.
 * Every measurement still checks the current reply's sensor bits and value. */
struct obd_egt_evidence { unsigned known, supported[OBD_EGT_BANKS]; };

/* Payload after 41 78/79: A support bits 0..3, then four big-endian temperatures
 * in 0.1 C, offset -40. Unsupported future flag bits and nonexact lengths fail.
 * -40..1800 C is this app's plausibility envelope, not a BMW operating limit. */
static inline int obd_egt_decode(const unsigned char* data, size_t n, struct obd_egt_sample* out) {
    if (!out) return 0;
    memset(out,0,sizeof *out);
    if (!data || n!=OBD_EGT_BYTES || (data[0]&0xf0)) return 0;
    out->present=data[0];
    for (unsigned i=0;i<OBD_EGT_SENSORS;++i) {
        if (!(out->present & (1u<<i))) continue;
        const unsigned raw=((unsigned)data[1+2*i]<<8)|data[2+2*i];
        const double value=raw*.1-40;
        if (value>1800) continue;
        out->celsius[i]=value;
        out->valid|=1u<<i;
    }
    return 1;
}

#endif
