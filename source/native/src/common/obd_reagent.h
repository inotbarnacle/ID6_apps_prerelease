#ifndef OBD_REAGENT_H
#define OBD_REAGENT_H
#include <stddef.h>
#include <string.h>

enum { OBD_REAGENT_FIELDS=3, OBD_REAGENT_RETRY_MS=5000 };
struct obd_reagent_sample { unsigned present; double value[OBD_REAGENT_FIELDS]; };

/* PID 85 data after 41 85: exactly ten bytes. Fields here are tank level (%),
 * average consumption (L/h), average demanded consumption (L/h). Byte A bits
 * 2/0/1 support them independently. Warning-runtime bytes are not app gauges.
 * Full protocol domains are retained; no inferred range, health or threshold. */
static inline int obd_reagent_decode(const unsigned char *data,size_t size,struct obd_reagent_sample *out) {
    static const unsigned bits[]={2,0,1},offsets[]={5,1,3};
    if (!out) return 0;
    memset(out,0,sizeof *out);
    if (!data || size!=10 || (data[0]&0xf0)) return 0;
    for (unsigned field=0;field<OBD_REAGENT_FIELDS;++field) {
        if (!(data[0] & (1u<<bits[field]))) continue;
        const unsigned offset=offsets[field];
        out->present|=1u<<field;
        out->value[field]=field==0 ? data[offset]*100.0/255 :
            (((unsigned)data[offset]<<8)|data[offset+1])/200.0;
    }
    return 1;
}
#endif
