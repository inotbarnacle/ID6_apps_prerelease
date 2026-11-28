#include "boost_defaults.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Standard decoders only. No DPF or unverified BMW oil source.
 * RPM occupies the narrow ninth tile when advertised; the other tiles follow
 * this priority. The order is an application choice, not an N57 ECU profile. */
static const struct { unsigned char signal, pid; } candidates[]={
    {BD_BOOST,0x0b}, {BD_OIL_TEMP,0x5c}, {BD_COOLANT,0x05},
    {BD_TRANS,0}, {BD_RAIL,0x23}, {BD_IAT,0x0f},
    {BD_EGT_B1S1,0x78}, {BD_EGT_B1S2,0x78}, {BD_EGT_B1S3,0x78}, {BD_EGT_B1S4,0x78},
    {BD_EGT_B2S1,0x79}, {BD_EGT_B2S2,0x79}, {BD_EGT_B2S3,0x79}, {BD_EGT_B2S4,0x79},
    {BD_MAF,0x10},
    {BD_LOAD,0x04}, {BD_BATTERY,0x42}, {BD_FUEL_RATE,0x5e},
    {BD_BARO,0x33}, {BD_MAP,0x0b}, {BD_RPM,0x0c}
};

bd_mask bd_diesel_candidates(void) {
    bd_mask mask=0;
    unsigned i;
    for (i=0;i<sizeof candidates/sizeof candidates[0];++i) mask|=BD_BIT(candidates[i].signal);
    return mask;
}

void bd_diesel_masks(const struct obd_capabilities *caps, int trans_observed,
                     const struct obd_egt_evidence *egt, bd_mask *known, bd_mask *supported) {
    unsigned i;
    *known=*supported=0;
    for (i=0;i<sizeof candidates/sizeof candidates[0];++i) {
        const unsigned signal=candidates[i].signal;
        const bd_mask bit=BD_BIT(signal);
        int status;
        if (signal==BD_TRANS) status=trans_observed ? 1 : OBD_CAPABILITY_PENDING;
        else {
            status=obd_pid_status(caps,candidates[i].pid);
            if (signal>=BD_EGT_B1S1 && status==1) {
                const unsigned bank=(signal-BD_EGT_B1S1)/OBD_EGT_SENSORS;
                const unsigned sensor=(signal-BD_EGT_B1S1)%OBD_EGT_SENSORS;
                status=!egt || !(egt->known & (1u<<bank)) ? OBD_CAPABILITY_PENDING :
                    (egt->supported[bank] & (1u<<sensor))!=0;
            }
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

void bd_diesel_slots(bd_mask supported, unsigned char slots[BD_DEFAULT_SLOTS]) {
    unsigned i, next=0, limit=BD_DEFAULT_SLOTS;
    int have_egt=0;
    memset(slots,BD_EMPTY_SLOT,BD_DEFAULT_SLOTS);
    if (supported & BD_BIT(BD_RPM)) { slots[8]=BD_RPM; limit=8; }
    for (i=0;i<sizeof candidates/sizeof candidates[0] && next<limit;++i) {
        const unsigned signal=candidates[i].signal;
        if (signal==BD_RPM || !(supported & BD_BIT(signal))) continue;
        if (signal>=BD_EGT_B1S1) {
            if (have_egt) continue; /* one EGT in defaults; custom layouts may choose more */
            have_egt=1;
        }
        slots[next++]=(unsigned char)signal;
    }
}

bd_mask bd_slots_mask(const unsigned char slots[BD_DEFAULT_SLOTS]) {
    unsigned i;
    bd_mask mask=0;
    for (i=0;i<BD_DEFAULT_SLOTS;++i) if (slots[i]<BD_COUNT) mask|=BD_BIT(slots[i]);
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
    const char* version=(frame->known & ~BD_LEGACY_MASK) ? "NBC2" : "NBC1";
    n=snprintf(out,size,"%s %llu %lu %llu %llu %llu\n",version,(unsigned long long)frame->token,
               (unsigned long)frame->worker_pid,(unsigned long long)frame->sent_ms,
               (unsigned long long)frame->known,(unsigned long long)frame->supported);
    return n<0 || (size_t)n>=size ? -1 : n;
}
int bd_cap_parse(const char *data, size_t size, bd_cap_frame *out) {
    char copy[BD_CAP_PACKET], *cursor, *end;
    uint64_t fields[5];
    bd_cap_frame frame;
    unsigned i;
    if (!data || !out || size<6 || size>=sizeof copy || memchr(data,0,size) ||
        (memcmp(data,"NBC1",4) && memcmp(data,"NBC2",4)) || !isspace((unsigned char)data[4])) return 0;
    const int legacy=!memcmp(data,"NBC1",4);
    memcpy(copy,data,size); copy[size]=0; cursor=copy+4;
    for (i=0;i<5;++i) {
        while (isspace((unsigned char)*cursor)) ++cursor;
        if (!isdigit((unsigned char)*cursor)) return 0;
        errno=0; fields[i]=strtoull(cursor,&end,10);
        if (errno==ERANGE || end==cursor || (*end && !isspace((unsigned char)*end))) return 0;
        cursor=end;
    }
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor || fields[1]>INT32_MAX || (legacy && ((fields[3]|fields[4]) & ~BD_LEGACY_MASK))) return 0;
    frame.token=fields[0]; frame.worker_pid=(uint32_t)fields[1]; frame.sent_ms=fields[2];
    frame.known=fields[3]; frame.supported=fields[4];
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
