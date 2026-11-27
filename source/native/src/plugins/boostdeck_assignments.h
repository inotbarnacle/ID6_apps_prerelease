#ifndef BOOSTDECK_ASSIGNMENTS_H
#define BOOSTDECK_ASSIGNMENTS_H
#include "gauge_core.h"
#include "n57_gauge_support.h"
#include "gauge_preferences.h"
#include <string.h>

enum { kBoostDeckSlots=9 };
inline bool boostDeckSignalHidden(unsigned signal,bool diesel,bool hideEmissions,bool n57=false) {
    if (signal==BD_REL_TORQUE_DDE && (!diesel || !n57)) return true;
    return diesel && ((n57 && !n57_gauge_supported(signal)) ||
        (hideEmissions && (signal==BD_ADBLUE || signal==BD_DPF_SOOT ||
                          signal==BD_DPF_DP || signal==BD_DPF_REGEN_DIST)));
}
inline bd_mask boostDeckSelectionMask(const unsigned char* signals,bool diesel,bool hidden,bool n57=false) {
    bd_mask mask=0;
    for (unsigned i=0;i<kBoostDeckSlots;++i)
        if (signals[i]<BD_COUNT && !boostDeckSignalHidden(signals[i],diesel,hidden,n57)) mask|=BD_BIT(signals[i]);
    return mask;
}
inline bool migrateN57EngineLoad(unsigned char (&signals)[2][kBoostDeckSlots]) {
    bool changed=false;
    for (unsigned i=0;i<kBoostDeckSlots;++i)
        if (signals[1][i]==BD_LOAD) { signals[1][i]=BD_TORQUE;changed=true; }
    return changed;
}
inline void loadBoostDeckAssignments(const char* path,unsigned char (&signals)[2][kBoostDeckSlots],
                                    bool& diesel,bool& hidden,bool n57=false) {
    const unsigned char defaults[]={BD_BOOST,BD_LOAD,BD_RPM,BD_IAT,BD_TRANS,BD_RAIL,BD_MAF,BD_COOLANT,BD_MAP};
    diesel=hidden=false;
    for (unsigned i=0;i<kBoostDeckSlots;++i) signals[0][i]=static_cast<unsigned char>(i);
    memcpy(signals[1],defaults,sizeof defaults);
    if (n57) migrateN57EngineLoad(signals);
    FILE* file=fopen(path,"rb");
    if (!file) return;
    unsigned char bytes[59];
    const size_t length=fread(bytes,1,sizeof bytes,file);
    const bool complete=fgetc(file)==EOF && !ferror(file);
    fclose(file);
    if (!complete || length<3 || bytes[0]!='B' || bytes[2]>1) return;
    unsigned offset=3,stride=9,pages=1,page=0;
    if (bytes[1]==1 && length==25) stride=11;
    else if (bytes[1]==2 && length==21) {}
    else if ((bytes[1]==3 || bytes[1]==5) && length==22 && bytes[3]<=1) offset=4;
    else if (bytes[1]==4 && length==59 && bytes[3]<=1 && bytes[4]<3) {
        offset=5;pages=3;page=bytes[4];
    } else return;
    for (unsigned profile=0;profile<2;++profile)
        for (unsigned panel=0;panel<pages;++panel)
            for (unsigned i=0;i<kBoostDeckSlots;++i)
                if (bytes[offset+(profile*pages+panel)*stride+i]>=BD_COUNT) return;
    diesel=bytes[2]!=0;hidden=offset>=4 && bytes[3]!=0;
    // Flatten the saved startup panel when migrating the former multi-panel fork.
    for (unsigned profile=0;profile<2;++profile)
        memcpy(signals[profile],bytes+offset+(profile*pages+page)*stride,kBoostDeckSlots);
    if (n57) migrateN57EngineLoad(signals);
}
inline bool saveBoostDeckAssignments(const char* path,const unsigned char (&signals)[2][kBoostDeckSlots],
                                    bool diesel,bool hidden) {
    for (unsigned p=0;p<2;++p) for (unsigned i=0;i<kBoostDeckSlots;++i)
        if (signals[p][i]>=BD_COUNT) return false;
    unsigned char bytes[4+sizeof signals]={'B',5,static_cast<unsigned char>(diesel),static_cast<unsigned char>(hidden)};
    memcpy(bytes+4,signals,sizeof signals);
    return savePersistentBytes(path,bytes,sizeof bytes);
}
#endif
