#ifndef ENGINE_FAULT_CODES_H
#define ENGINE_FAULT_CODES_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ISO 14229 status bits; protocol provenance and limits: docs/FAULT-CODES.md. */
enum {
    FAULT_TEST_FAILED = 0x01, FAULT_FAILED_THIS_CYCLE = 0x02,
    FAULT_PENDING = 0x04, FAULT_CONFIRMED = 0x08,
    FAULT_NOT_COMPLETED_SINCE_CLEAR = 0x10, FAULT_FAILED_SINCE_CLEAR = 0x20,
    FAULT_NOT_COMPLETED_THIS_CYCLE = 0x40, FAULT_WARNING_REQUESTED = 0x80,
    FAULT_REQUEST_MASK = 0xaf, FAULT_REPLY_MAX = 4094
};
struct fault_reply {
    const unsigned char* records;
    size_t count;
    unsigned available;
};
struct fault_record { uint32_t code; unsigned status, available; };

/* Complete 59 02 responses only. A transport-limited MOST prefix is trimmed
 * explicitly by the caller and must retain its "more may exist" notice. */
static inline int fault_parse_reply(const unsigned char* data, size_t n, struct fault_reply* out) {
    if (!out) return 0;
    memset(out,0,sizeof *out);
    if (!data || n < 3 || n > FAULT_REPLY_MAX || data[0] != 0x59 || data[1] != 2 || (n-3)%4) return 0;
    out->records=data+3;
    out->available=data[2];
    out->count=(n-3)/4;
    return 1;
}
static inline int fault_record_at(const struct fault_reply* reply, size_t index, struct fault_record* out) {
    if (!reply || !out || !reply->records || index >= reply->count) return 0;
    const unsigned char* p=reply->records+index*4;
    out->code=((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
    out->status=p[3];
    out->available=reply->available;
    return 1;
}

#endif
