#include "gauge_core.h"
#include "obd_egr.h"
#include "fixtures/boost_egt_frame.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    struct obd_egr_sample sample;
    unsigned char data[8]={63,0,255,128,128,0,255,0};
    assert(obd_egr_decode(data,7,&sample) && sample.present==63);
    assert(sample.value[0]==0 && sample.value[1]==100 && sample.value[2]==0);
    assert(fabs(sample.value[3]-50.196078431)<.000001 && sample.value[4]==0 && sample.value[5]==99.21875);
    for (unsigned flags=0;flags<256;++flags) {
        data[0]=(unsigned char)flags;
        assert(obd_egr_decode(data,7,&sample)==(flags<64));
        assert(sample.present==(flags<64 ? flags : 0));
        for (unsigned field=0;field<6;++field)
            if (!(sample.present & (1u<<field))) assert(sample.value[field]==0);
    }
    data[0]=63;
    for (size_t n=0;n<=sizeof data;++n) assert(obd_egr_decode(data,n,&sample)==(n==7));
    assert(!obd_egr_decode(0,7,&sample) && !obd_egr_decode(data,7,0));
    assert(!obd_egr_decode(data,(size_t)-1,&sample));
    for (unsigned raw=0;raw<256;++raw) {
        double commanded,error;
        unsigned char b=(unsigned char)raw;
        assert(bd_decode_obd(0x2c,&b,1,&commanded) && bd_decode_obd(0x2d,&b,1,&error));
        for (unsigned i=1;i<7;++i) data[i]=b;
        assert(obd_egr_decode(data,7,&sample));
        for (unsigned i=0;i<6;++i) assert(fabs(sample.value[i]-(i%3==2 ? error : commanded))<.0000001);
        assert(commanded>=0 && commanded<=100 && error>=-100 && error<=99.21875);
        assert(!bd_decode_obd(0x2c,&b,0,&commanded) && !bd_decode_obd(0x2d,data,2,&error));
    }
    data[0]=1; // commanded supported; a zero-filled actual field is absent
    assert(obd_egr_decode(data,7,&sample) && sample.present==1 && sample.value[1]==0);

    bd_frame old,frame;
    memset(&old,0,sizeof old); old.seq=15; old.sent_ms=456789;
    old.valid=BD_BIT(BD_EGT_B1S1)|BD_BIT(BD_EGT_B2S4);
    for (unsigned i=0;i<BD_WIRE8_COUNT;++i) { old.value[i]=i*10+.5; old.age_ms[i]=i*17; }
    char wire[BD_MAX_PACKET];
    int n=bd_encode(&old,wire,sizeof wire);
    assert(n==(int)strlen(legacy_egt_packet) && !memcmp(wire,legacy_egt_packet,(size_t)n));
    assert(bd_parse(wire,(size_t)n,&frame) && frame.valid==old.valid);
    assert(!frame.value[BD_EGR_REQUEST] && !frame.age_ms[BD_EGR_B_ERROR]);
    memset(&frame,0,sizeof frame); frame.sent_ms=12345; frame.valid=BD_ALL_MASK;
    frame.value[BD_EGR_REQUEST]=100; frame.value[BD_EGR_ERROR]=-100;
    frame.value[BD_EGR_A_ACTUAL]=0; frame.value[BD_EGR_B_ERROR]=99.21875;
    n=bd_encode(&frame,wire,sizeof wire);
    assert(n>0 && n<BD_MAX_PACKET && !memcmp(wire,"NBG9 ",5));
    assert(bd_parse(wire,(size_t)n,&old) && old.valid==BD_ALL_MASK);
    assert(old.value[BD_EGR_A_ACTUAL]==0 && old.value[BD_EGR_B_ERROR]==99.21875);
    assert(bd_is_fresh(&old,BD_EGR_B_ERROR,3499) && !bd_is_fresh(&old,BD_EGR_B_ERROR,3500));
    assert(bd_display_value(BD_EGR_A_ACTUAL,50,0)==50 && !strcmp(bd_unit(BD_EGR_ERROR,0),"%"));
    assert(bd_bar(BD_EGR_A_ACTUAL,50)==500 && bd_bar(BD_EGR_ERROR,0)==500);
    for (int length=0;length<n;++length) assert(!bd_parse(wire,(size_t)length,&old));
    wire[3]='8'; assert(!bd_parse(wire,(size_t)n,&old)); wire[3]='9';
    frame.value[BD_EGR_A_ACTUAL]=-1;
    n=bd_encode(&frame,wire,sizeof wire); assert(n>0 && !bd_parse(wire,(size_t)n,&old));
    frame.value[BD_EGR_A_ACTUAL]=0; frame.value[BD_EGR_B_ERROR]=100;
    n=bd_encode(&frame,wire,sizeof wire); assert(n>0 && !bd_parse(wire,(size_t)n,&old));
    memset(&frame,0,sizeof frame); frame.valid=BD_BIT(BD_EGT_B1S1);
    n=bd_encode(&frame,wire,sizeof wire); assert(n>0 && n<BD_WIRE8_PACKET);
    memset(wire+n,' ',BD_WIRE8_PACKET-(size_t)n); wire[BD_WIRE8_PACKET-1]='\n';
    assert(!bd_parse(wire,BD_WIRE8_PACKET,&old)); // old packet bound is retained
    frame.valid=BD_BIT(63); assert(bd_encode(&frame,wire,sizeof wire)<0);
    puts("EGR support/scale bounds, valid zero, command/actual/error distinction, legacy EGT bytes and NBG9 framing passed.");
    return 0;
}
