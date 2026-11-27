#ifndef FAULT_DISPLAY_H
#define FAULT_DISPLAY_H

#include "fault_codes.h"

struct FaultLine {
    uint32_t code;
    unsigned status, available;
    bool has_mask;
};

inline bool faultHex(const char* data, unsigned length, unsigned& out) {
    unsigned value=0;
    for (unsigned i=0;i<length;++i) {
        unsigned digit;
        if (data[i]>='0' && data[i]<='9') digit=data[i]-'0';
        else if (data[i]>='A' && data[i]<='F') digit=data[i]-'A'+10;
        else if (data[i]>='a' && data[i]<='f') digit=data[i]-'a'+10;
        else return false;
        value=(value<<4)|digit;
    }
    out=value;
    return true;
}
inline bool parseFaultLine(const char* line, size_t length, FaultLine& out) {
    if (!line || (length!=17 && length!=25) || memcmp(line+6,"  status ",9)) return false;
    unsigned code,status,available=0;
    if (!faultHex(line,6,code) || !faultHex(line+15,2,status)) return false;
    if (length==25 && (memcmp(line+17," mask ",6) || !faultHex(line+23,2,available))) return false;
    out.code=code;
    out.status=status;
    out.available=available;
    out.has_mask=length==25;
    return true;
}
inline const char* faultState(const FaultLine& line) {
    if (!line.has_mask) return "Raw"; // old worker: availability is not established
    const unsigned flags=line.status & line.available;
    if (flags & FAULT_TEST_FAILED) return "Active";
    if (flags & FAULT_PENDING) return "Pending";
    if (flags & FAULT_CONFIRMED) return "Confirmed";
    if (flags & FAULT_FAILED_THIS_CYCLE) return "This cycle";
    if (flags & FAULT_FAILED_SINCE_CLEAR) return "Since clear";
    if (flags & FAULT_WARNING_REQUESTED) return "Warning";
    return "Unknown";
}

#endif
