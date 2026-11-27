#include "gauge_core.h"
#include "boost_defaults.h"
#include "obd_egt.h"
#include "fixtures/boost_legacy_frame.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* Synthetic raw values: -40, 0, 600 and the app plausibility ceiling. */
    const unsigned char data[]={0x0f,0,0,0x01,0x90,0x19,0x00,0x47,0xe0};
    struct obd_egt_sample sample;
    assert(obd_egt_decode(data,sizeof data,&sample));
    assert(sample.present==15 && sample.valid==15);
    assert(sample.celsius[0]==-40 && sample.celsius[1]==0);
    assert(fabs(sample.celsius[2]-600)<.0001 && fabs(sample.celsius[3]-1800)<.0001);
    unsigned char packet_data[OBD_EGT_BYTES+1]; memcpy(packet_data,data,sizeof data);
    for (unsigned presence=0;presence<256;++presence) {
        packet_data[0]=(unsigned char)presence;
        assert(obd_egt_decode(packet_data,sizeof data,&sample)==(presence<16));
        if (presence<16) {
            assert(sample.present==presence && sample.valid==presence);
            for (unsigned i=0;i<4;++i) if (!(presence&(1u<<i))) assert(sample.celsius[i]==0);
        } else assert(!sample.present && !sample.valid);
    }
    packet_data[0]=15;
    for (size_t n=0;n<=sizeof packet_data;++n)
        assert(obd_egt_decode(packet_data,n,&sample)==(n==OBD_EGT_BYTES));
    packet_data[7]=0x47; packet_data[8]=0xe1; // 1800.1 C is outside the app envelope
    assert(obd_egt_decode(packet_data,9,&sample) && sample.present==15 && sample.valid==7);
    packet_data[5]=packet_data[6]=0xff;
    assert(obd_egt_decode(packet_data,9,&sample) && sample.valid==3);
    assert(!obd_egt_decode(0,9,&sample) && !obd_egt_decode(data,9,0));
    assert(!obd_egt_decode(data,(size_t)-1,&sample));

    bd_frame legacy, decoded;
    memset(&legacy,0,sizeof legacy); legacy.seq=42; legacy.valid=BD_LEGACY_MASK; legacy.sent_ms=123456;
    for (unsigned i=0;i<BD_LEGACY_COUNT;++i) { legacy.value[i]=i*10+.5; legacy.age_ms[i]=i*37; }
    char wire[BD_AUTO_PACKET];
    int n=bd_encode(&legacy,wire,sizeof wire);
    assert(n==(int)strlen(legacy_boost_packet) && !memcmp(wire,legacy_boost_packet,(size_t)n));
    assert(bd_parse(wire,(size_t)n,&decoded) && decoded.valid==BD_LEGACY_MASK);
    assert(bd_parse(wire,(size_t)n-1,&decoded)); // legacy framing still accepts a missing final newline
    for (unsigned i=BD_LEGACY_COUNT;i<BD_COUNT;++i) assert(!decoded.value[i] && !decoded.age_ms[i]);

    legacy.valid=BD_ALL_MASK;
    for (unsigned i=BD_EGT_B1S1;i<BD_COUNT;++i) { legacy.value[i]=600.5+i; legacy.age_ms[i]=123; }
    n=bd_encode(&legacy,wire,sizeof wire);
    assert(n>0 && n<BD_MAX_PACKET && !memcmp(wire,"NBG8 ",5));
    assert(bd_parse(wire,(size_t)n,&decoded) && decoded.valid==BD_ALL_MASK);
    assert(decoded.value[BD_EGT_B2S4]==legacy.value[BD_EGT_B2S4]);
    assert(bd_is_fresh(&decoded,BD_EGT_B2S4,3376) && !bd_is_fresh(&decoded,BD_EGT_B2S4,3377));
    assert(fabs(bd_display_value(BD_EGT_B2S4,100,0)-212)<.0001);
    assert(bd_bar(BD_EGT_B2S4,600)==500 && bd_bar(BD_EGT_B2S4,1800)==1000);
    for (int length=0;length<n;++length) assert(!bd_parse(wire,(size_t)length,&decoded));
    wire[3]='7'; assert(!bd_parse(wire,(size_t)n,&decoded)); wire[3]='8';
    legacy.valid=BD_BIT(63); assert(bd_encode(&legacy,wire,sizeof wire)<0);

    struct obd_capabilities caps={{1,1,1,0x180},{0},15}; // PID 78/79 advertisements
    struct obd_egt_evidence evidence={0,{0,0}};
    bd_mask known,supported;
    const bd_mask egt_mask=BD_ALL_MASK & ~BD_LEGACY_MASK;
    bd_diesel_masks(&caps,0,&evidence,&known,&supported);
    assert(!(known&egt_mask) && !(supported&egt_mask)); // PID alone cannot select sensors
    evidence.known=3; evidence.supported[0]=10; evidence.supported[1]=1;
    bd_diesel_masks(&caps,0,&evidence,&known,&supported);
    assert((known&egt_mask)==egt_mask);
    assert((supported&egt_mask)==(BD_BIT(BD_EGT_B1S2)|BD_BIT(BD_EGT_B1S4)|BD_BIT(BD_EGT_B2S1)));
    unsigned char slots[9]; bd_diesel_slots(supported,slots);
    assert(slots[0]==BD_EGT_B1S2 && slots[1]==BD_EMPTY_SLOT);
    bd_cap_frame report={42,123456,123,known,supported}, parsed;
    legacy.valid=supported; legacy.sent_ms=report.sent_ms;
    n=bd_auto_encode(&report,&legacy,wire,sizeof wire);
    assert(n>0 && !memcmp(wire,"NBC2 ",5));
    assert(bd_auto_parse(wire,(size_t)n,&parsed,&decoded) && parsed.supported==supported);
    wire[3]='1'; assert(!bd_auto_parse(wire,(size_t)n,&parsed,&decoded));
    report.known&=BD_LEGACY_MASK; report.supported=0; legacy.valid=0;
    n=bd_auto_encode(&report,&legacy,wire,sizeof wire);
    assert(n>0 && !memcmp(wire,"NBC1 ",5) && bd_auto_parse(wire,(size_t)n,&parsed,&decoded));
    puts("EGT sensor masks/scales/bounds, legacy byte equivalence, wide frames, capability evidence and default grouping passed.");
    return 0;
}
