#include "gauge_core.h"
#include "obd_pressure.h"
#include "fixtures/boost_egr_frame.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    struct obd_pressure_sample sample;
    unsigned char rail[12]={0x3f,0x27,0x10,0x23,0x28,0xff,0x4e,0x20,0xff,0xff,0xff,0};
    unsigned char boost[11]={0x3f,0x19,0,0x19,0x2a,0x32,0,0xff,0xff,0xff,0};
    assert(obd_pressure_decode(0x6d,rail,11,&sample) && sample.present==15);
    assert(sample.value[0]==1000 && sample.value[1]==900 && sample.value[2]==2000 && sample.value[3]==6553.5);
    assert(obd_pressure_decode(0x70,boost,10,&sample) && sample.present==15);
    assert(sample.value[0]==200 && sample.value[1]==201.3125 && sample.value[2]==400 && sample.value[3]==2047.96875);
    // Independently published preOBD encoder vector: support actual A, raw 6442.
    boost[0]=2;
    assert(obd_pressure_decode(0x70,boost,10,&sample) && sample.present==2 && sample.value[1]==201.3125);
    for (unsigned group=0;group<2;++group) {
        unsigned char* data=group ? boost : rail;
        const unsigned pid=group ? 0x70 : 0x6d, length=group ? 10 : 11;
        for (unsigned flags=0;flags<256;++flags) {
            data[0]=(unsigned char)flags;
            const unsigned expected=(flags&3)|((flags>>1)&12);
            assert(obd_pressure_decode(pid,data,length,&sample)==(flags<64));
            assert(sample.present==(flags<64 ? expected : 0));
            for (unsigned field=0;field<4;++field)
                if (!(sample.present & (1u<<field))) assert(sample.value[field]==0);
        }
        data[0]=0x3f;
        for (size_t n=0;n<=length+1;++n) assert(obd_pressure_decode(pid,data,n,&sample)==(n==length));
        assert(!obd_pressure_decode(pid,0,length,&sample) && !sample.present);
        assert(!obd_pressure_decode(pid,data,length,0));
        assert(!obd_pressure_decode(pid,data,(size_t)-1,&sample));
        // Entire unsigned domain, including zero and FFFF, has defined scaling.
        for (unsigned raw=0;raw<=65535;++raw) {
            for (unsigned field=0;field<4;++field) {
                const unsigned offset=1+2*field+(!group && field>=2 ? 1 : 0);
                data[offset]=(unsigned char)(raw>>8); data[offset+1]=(unsigned char)raw;
            }
            assert(obd_pressure_decode(pid,data,length,&sample) && sample.present==15);
            for (unsigned field=0;field<4;++field)
                assert(sample.value[field]==(group ? raw*.03125 : raw*.1) ||
                       fabs(sample.value[field]-(group ? raw*.03125 : raw*.1))<1e-9);
        }
    }
    assert(!obd_pressure_decode(0x6e,rail,11,&sample) && !sample.present);
    assert(!obd_pressure_decode(0x6f,rail,11,&sample));

    bd_frame old,frame;
    memset(&old,0,sizeof old); old.seq=19; old.sent_ms=567890;
    old.valid=BD_BIT(BD_EGR_A_ACTUAL)|BD_BIT(BD_EGR_B_ERROR);
    for (unsigned i=0;i<BD_WIRE9_COUNT;++i) { old.value[i]=i<39 ? i*10+.5 : (int)i-40; old.age_ms[i]=i*13; }
    char wire[BD_MAX_PACKET]; int n=bd_encode(&old,wire,sizeof wire);
    assert(n==(int)strlen(legacy_egr_packet) && !memcmp(wire,legacy_egr_packet,(size_t)n));
    assert(bd_parse(wire,(size_t)n,&frame) && frame.valid==old.valid && !frame.value[BD_RAIL_A_REQUEST]);
    memset(wire+n,' ',BD_WIRE9_PACKET-(size_t)n); wire[BD_WIRE9_PACKET-1]='\n';
    assert(!bd_parse(wire,BD_WIRE9_PACKET,&frame));
    memset(&frame,0,sizeof frame); frame.seq=UINT32_MAX; frame.sent_ms=UINT64_MAX; frame.valid=BD_WIRE10_MASK;
    frame.value[BD_RAIL_B_ACTUAL]=6553.5; frame.value[BD_BOOST_B_ACTUAL]=2047.96875;
    for (unsigned i=0;i<BD_COUNT;++i) frame.age_ms[i]=999999;
    n=bd_encode(&frame,wire,sizeof wire);
    assert(n>0 && n<BD_MAX_PACKET && !memcmp(wire,"NBG10 ",6));
    assert(bd_parse(wire,(size_t)n,&old) && old.valid==BD_WIRE10_MASK && old.sent_ms==UINT64_MAX);
    assert(old.value[BD_RAIL_B_ACTUAL]==6553.5 && old.value[BD_BOOST_B_ACTUAL]==2047.96875);
    for (int length=0;length<n;++length) assert(!bd_parse(wire,(size_t)length,&old));
    for (unsigned i=0;i<6;++i) {
        const char saved=wire[i]; wire[i]='x'; assert(!bd_parse(wire,(size_t)n,&old)); wire[i]=saved;
    }
    char oversized[BD_MAX_PACKET+1]; memcpy(oversized,wire,(size_t)n);
    memset(oversized+n,' ',sizeof oversized-(size_t)n); oversized[sizeof oversized-1]='\n';
    assert(!bd_parse(oversized,sizeof oversized,&old));
    memcpy(oversized,"NBG100",6); assert(!bd_parse(oversized,(size_t)n,&old));
    frame.value[BD_BOOST_A_ACTUAL]=-0.1;
    n=bd_encode(&frame,wire,sizeof wire); assert(n>0 && !bd_parse(wire,(size_t)n,&old));
    frame.value[BD_BOOST_A_ACTUAL]=2048;
    n=bd_encode(&frame,wire,sizeof wire); assert(n>0 && !bd_parse(wire,(size_t)n,&old));
    frame.value[BD_BOOST_A_ACTUAL]=0; frame.value[BD_RAIL_A_ACTUAL]=6553.6;
    n=bd_encode(&frame,wire,sizeof wire); assert(n>0 && !bd_parse(wire,(size_t)n,&old));
    frame.valid=BD_BIT(BD_COUNT); assert(bd_encode(&frame,wire,sizeof wire)<0);
    frame.valid=BD_BIT(BD_RAIL_A_ACTUAL); frame.age_ms[BD_RAIL_A_ACTUAL]=0;
    assert(bd_is_fresh(&frame,BD_RAIL_A_ACTUAL,3499) && !bd_is_fresh(&frame,BD_RAIL_A_ACTUAL,3500));
    assert(fabs(bd_display_selected(BD_RAIL_A_ACTUAL,100,0)-1450.37738)<1e-6);
    assert(bd_display_selected(BD_RAIL_A_ACTUAL,100,2)==10000);
    assert(bd_display_selected(BD_BOOST_A_ACTUAL,200,1)==2);
    assert(bd_bar(BD_RAIL_A_ACTUAL,1000)==500 && bd_bar(BD_RAIL_A_ACTUAL,6553.5)==1000);
    assert(bd_bar(BD_BOOST_A_ACTUAL,250)==500 && bd_bar(BD_BOOST_A_ACTUAL,2047.96875)==1000);
    puts("Pressure flags, full raw domain, reference/units, frozen NBG9 bytes, strict NBG10 and freshness passed.");
    return 0;
}
