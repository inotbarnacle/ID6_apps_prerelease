#include "boost_defaults.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Existing, standard decoders only. No DPF, EGT or unverified BMW oil source.
 * RPM occupies the narrow ninth tile when advertised; the other tiles follow
 * this priority. The order is an application choice, not an N57 ECU profile. */
static const struct { unsigned char signal, pid; } candidates[]={
    {BD_BOOST,0x0b}, {BD_OIL_TEMP,0x5c}, {BD_COOLANT,0x05},
    {BD_TRANS,0}, {BD_RAIL,0x23}, {BD_IAT,0x0f}, {BD_MAF,0x10},
    {BD_LOAD,0x04}, {BD_BATTERY,0x42}, {BD_FUEL_RATE,0x5e},
    {BD_BARO,0x33}, {BD_MAP,0x0b}, {BD_RPM,0x0c}
};

unsigned bd_diesel_candidates(void) {
    unsigned mask=0, i;
    for (i=0;i<sizeof candidates/sizeof candidates[0];++i) mask|=1u<<candidates[i].signal;
    return mask;
}

void bd_diesel_masks(const struct obd_capabilities *caps, int trans_observed,
                     unsigned *known, unsigned *supported) {
    unsigned i;
    *known=*supported=0;
    for (i=0;i<sizeof candidates/sizeof candidates[0];++i) {
        const unsigned signal=candidates[i].signal, bit=1u<<signal;
        int status;
        if (signal==BD_TRANS) status=trans_observed ? 1 : OBD_CAPABILITY_PENDING;
        else {
            status=obd_pid_status(caps,candidates[i].pid);
            if (signal==BD_BOOST) {
                const int baro=obd_pid_status(caps,0x33);
                if (!status || !baro) status=0;
                else if (status!=1 || baro!=1) status=OBD_CAPABILITY_PENDING;
            }
        }
        if (status==OBD_CAPABILITY_PENDING) continue;
        *known|=bit;
        if (status==1) *supported|=bit;
    }
}

void bd_diesel_slots(unsigned supported, unsigned char slots[BD_DEFAULT_SLOTS]) {
    unsigned i, next=0, limit=BD_DEFAULT_SLOTS;
    memset(slots,BD_EMPTY_SLOT,BD_DEFAULT_SLOTS);
    if (supported & (1u<<BD_RPM)) { slots[8]=BD_RPM; limit=8; }
    for (i=0;i<sizeof candidates/sizeof candidates[0] && next<limit;++i) {
        const unsigned signal=candidates[i].signal;
        if (signal!=BD_RPM && (supported & (1u<<signal))) slots[next++]=(unsigned char)signal;
    }
}

unsigned bd_slots_mask(const unsigned char slots[BD_DEFAULT_SLOTS]) {
    unsigned i, mask=0;
    for (i=0;i<BD_DEFAULT_SLOTS;++i) if (slots[i]<BD_COUNT) mask|=1u<<slots[i];
    return mask;
}

static int valid(const bd_cap_frame *frame) {
    return frame && frame->token && frame->worker_pid && frame->worker_pid<=INT32_MAX &&
        frame->sent_ms && !(frame->known & ~bd_diesel_candidates()) &&
        !(frame->supported & ~frame->known);
}
int bd_cap_encode(const bd_cap_frame *frame, char *out, size_t size) {
    int n;
    if (!out || !size || !valid(frame)) return -1;
    n=snprintf(out,size,"NBC1 %llu %lu %llu %lu %lu\n",(unsigned long long)frame->token,
               (unsigned long)frame->worker_pid,(unsigned long long)frame->sent_ms,
               (unsigned long)frame->known,(unsigned long)frame->supported);
    return n<0 || (size_t)n>=size ? -1 : n;
}
int bd_cap_parse(const char *data, size_t size, bd_cap_frame *out) {
    char copy[BD_CAP_PACKET], *cursor, *end;
    uint64_t fields[5];
    bd_cap_frame frame;
    unsigned i;
    if (!data || !out || size<6 || size>=sizeof copy || memchr(data,0,size) ||
        memcmp(data,"NBC1",4) || !isspace((unsigned char)data[4])) return 0;
    memcpy(copy,data,size); copy[size]=0; cursor=copy+4;
    for (i=0;i<5;++i) {
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (!isdigit((unsigned char)*cursor)) return 0;
        errno=0; fields[i]=strtoull(cursor,&end,10);
        if (errno==ERANGE || end==cursor || (*end && !isspace((unsigned char)*end))) return 0;
        cursor=end;
    }
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor || fields[1]>INT32_MAX || fields[3]>UINT32_MAX || fields[4]>UINT32_MAX) return 0;
    frame.token=fields[0]; frame.worker_pid=(uint32_t)fields[1]; frame.sent_ms=fields[2];
    frame.known=(uint32_t)fields[3]; frame.supported=(uint32_t)fields[4];
    if (!valid(&frame)) return 0;
    *out=frame;
    return 1;
}

int bd_auto_encode(const bd_cap_frame *caps, const bd_frame *frame, char *out, size_t size) {
    int header, payload;
    if (!frame || frame->demo || !caps || caps->sent_ms!=frame->sent_ms) return -1;
    header=bd_cap_encode(caps,out,size);
    if (header<0) return -1;
    size_t available=size-(size_t)header;
    payload=bd_encode(frame,out+header,available<BD_MAX_PACKET ? available : BD_MAX_PACKET);
    return payload<0 ? -1 : header+payload;
}
int bd_auto_parse(const char *data, size_t size, bd_cap_frame *caps, bd_frame *frame) {
    const char *newline;
    size_t header;
    bd_cap_frame c;
    bd_frame f;
    if (!data || !caps || !frame || !size || size>=BD_AUTO_PACKET) return 0;
    newline=(const char *)memchr(data,'\n',size);
    if (!newline) return 0;
    header=(size_t)(newline-data)+1;
    if (!bd_cap_parse(data,header,&c) || !bd_parse(data+header,size-header,&f) ||
        f.demo || c.sent_ms!=f.sent_ms) return 0;
    *caps=c; *frame=f;
    return 1;
}
