#ifndef OBD_PRESSURE_H
#define OBD_PRESSURE_H

#include <stddef.h>
#include <string.h>

enum { OBD_PRESSURE_GROUPS=2, OBD_PRESSURE_FIELDS=4, OBD_PRESSURE_RETRY_MS=5000 };
struct obd_pressure_sample { unsigned present; double value[OBD_PRESSURE_FIELDS]; };

/* Data after 41 <pid>. Fields: A request/actual, B request/actual.
 * PID 6D: 11 bytes, rail gauge pressure in bar; temperature bytes unused.
 * PID 70: 10 bytes, boost absolute pressure in kPa; control status unused.
 * The per-field bits are 0,1,3,4, not a contiguous four-bit mask.
 * Source provenance, limits and A/B semantics: docs/STANDARD-PRESSURE.md. */
static inline int obd_pressure_decode(unsigned pid, const unsigned char* data, size_t n,
                                      struct obd_pressure_sample* out) {
    static const unsigned bits[]={0,1,3,4};
    if (!out) return 0;
    memset(out,0,sizeof *out);
    if (!data || (pid!=0x6d && pid!=0x70) || n!=(pid==0x6d ? 11u : 10u) || (data[0]&0xc0)) return 0;
    for (unsigned field=0;field<OBD_PRESSURE_FIELDS;++field) {
        if (!(data[0] & (1u<<bits[field]))) continue;
        const unsigned offset=1+field*2+(pid==0x6d && field>=2 ? 1 : 0);
        const unsigned raw=((unsigned)data[offset]<<8)|data[offset+1];
        out->present |= 1u<<field;
        out->value[field]=raw/(pid==0x6d ? 10.0 : 32.0);
    }
    return 1;
}

#endif
