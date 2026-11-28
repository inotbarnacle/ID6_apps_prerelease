#ifndef OBD_FAULT_CODES_H
#define OBD_FAULT_CODES_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* CAN-counted SAE J1979 replies only, not BMW's 24-bit 59 02 format.
 * Protocol sources and deliberate limits are in docs/FAULT-CODES.md. */
enum { OBD_FAULT_REPLY_MAX = 2 + 2 * 255 };
struct obd_fault_reply { const unsigned char* records; unsigned count; };

static inline int obd_fault_service(unsigned service) {
    return service==3 || service==7 || service==10;
}
static inline const char* obd_fault_state(unsigned service) {
    return service==3 ? "Confirmed" : service==7 ? "Pending" : service==10 ? "Permanent" : "Unknown";
}
static inline int obd_fault_parse_reply(const unsigned char* data, size_t n,
                                      unsigned service, struct obd_fault_reply* out) {
    if (!out) return 0;
    memset(out,0,sizeof *out);
    if (!obd_fault_service(service) || !data || n<2 || n>OBD_FAULT_REPLY_MAX ||
        (n&1) || data[0]!=(0x40u|service)) return 0;
    const unsigned count=data[1];
    const size_t needed=2u+2u*count;
    if (n<needed) return 0;
    /* 0000 is filler, never a reported fault. Do not silently fix a bad count. */
    for (unsigned i=0;i<count;++i)
        if (!(data[2+2*i] || data[3+2*i])) return 0;
    for (size_t i=needed;i<n;++i) if (data[i]) return 0;
    out->records=data+2;
    out->count=count;
    return 1;
}
static inline int obd_fault_record_at(const struct obd_fault_reply* reply, unsigned index, uint16_t* out) {
    if (!reply || !reply->records || !out || index>=reply->count) return 0;
    const unsigned char* p=reply->records+2u*index;
    *out=(uint16_t)(((unsigned)p[0]<<8)|p[1]);
    return 1;
}
static inline void obd_fault_format(uint16_t code, char out[6]) {
    static const char systems[]="PCBU", hex[]="0123456789ABCDEF";
    out[0]=systems[code>>14];
    out[1]=(char)('0'+((code>>12)&3));
    out[2]=hex[(code>>8)&15];
    out[3]=hex[(code>>4)&15];
    out[4]=hex[code&15];
    out[5]=0;
}

#endif
