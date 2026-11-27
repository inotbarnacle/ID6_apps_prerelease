#include "gauge_core.h"
#include "boost_defaults.h"
#include "obd_reagent.h"
#include "fixtures/boost_pressure_frame.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void near(double a,double b) { assert(fabs(a-b)<1e-6); }

int main(void) {
    struct obd_reagent_sample sample;
    unsigned char data[12]={7,1,0,0,200,128,255,255,255,255};
    assert(obd_reagent_decode(data,10,&sample) && sample.present==7);
    near(sample.value[0],128*100.0/255);near(sample.value[1],1.28);near(sample.value[2],1);
    for (unsigned flags=0;flags<256;++flags) {
        data[0]=(unsigned char)flags;
        assert(obd_reagent_decode(data,10,&sample)==(flags<16));
        assert(sample.present==(flags<16 ? ((flags&4)>>2)|((flags&1)<<1)|((flags&2)<<1) : 0));
    }
    data[0]=7;
    for (unsigned raw=0;raw<65536;++raw) {
        data[1]=data[3]=(unsigned char)(raw>>8);data[2]=data[4]=(unsigned char)raw;
        assert(obd_reagent_decode(data,10,&sample) && sample.present==7);
        near(sample.value[1],raw/200.0);near(sample.value[2],raw/200.0);
    }
    for (unsigned raw=0;raw<256;++raw) {
        data[5]=(unsigned char)raw;assert(obd_reagent_decode(data,10,&sample));
        near(sample.value[0],raw*100.0/255);
    }
    for (unsigned length=0;length<=12;++length) {
        if (length==10) continue;
        memset(&sample,0xff,sizeof sample);
        assert(!obd_reagent_decode(data,length,&sample) && !sample.present);
    }
    assert(!obd_reagent_decode(0,10,&sample) && !obd_reagent_decode(data,10,0));
    memset(data,0,sizeof data);data[0]=7;
    assert(obd_reagent_decode(data,10,&sample) && sample.present==7);
    for (unsigned i=0;i<3;++i) near(sample.value[i],0);
    data[0]=8;assert(obd_reagent_decode(data,10,&sample) && !sample.present); // runtime alone is no gauge

    assert(BD_REAGENT_LEVEL==55 && BD_REAGENT_AVERAGE==56 && BD_REAGENT_DEMAND==57 && BD_COUNT==58);
    bd_frame old,frame;char packet[BD_MAX_PACKET];
    assert(bd_parse(legacy_pressure_packet,sizeof legacy_pressure_packet-1,&old));
    assert(old.valid==BD_WIRE10_MASK && old.seq==UINT32_MAX && old.sent_ms==UINT64_MAX);
    for (unsigned i=0;i<BD_WIRE10_COUNT;++i) { near(old.value[i],i+.125);assert(old.age_ms[i]==i*17); }
    for (unsigned i=BD_WIRE10_COUNT;i<BD_COUNT;++i) assert(!bd_is_fresh(&old,i,0));
    int size=bd_encode(&old,packet,sizeof packet);
    assert(size==(int)sizeof legacy_pressure_packet-1 && !memcmp(packet,legacy_pressure_packet,(size_t)size));
    memset(packet+size,' ',BD_WIRE10_PACKET-(size_t)size);packet[BD_WIRE10_PACKET-1]='\n';
    assert(!bd_parse(packet,BD_WIRE10_PACKET,&frame)); // NBG10 retains its old bound
    frame=old;frame.valid=BD_ALL_MASK;
    frame.value[BD_REAGENT_LEVEL]=100;frame.value[BD_REAGENT_AVERAGE]=327.675;frame.value[BD_REAGENT_DEMAND]=.005;
    size=bd_encode(&frame,packet,sizeof packet);
    assert(size>0 && size<BD_MAX_PACKET && !memcmp(packet,"NBG11 ",6));
    assert(bd_parse(packet,(size_t)size,&old) && old.valid==BD_ALL_MASK);
    near(old.value[BD_REAGENT_DEMAND],.005);
    for (int length=0;length<size;++length) assert(!bd_parse(packet,(size_t)length,&old));
    packet[4]='0';assert(!bd_parse(packet,(size_t)size,&old));packet[4]='1';
    const double bad[]={-.001,100.001,327.676};
    for (unsigned i=0;i<3;++i) {
        bd_frame invalid=frame;invalid.value[i==2 ? BD_REAGENT_AVERAGE : BD_REAGENT_LEVEL]=bad[i];
        size=bd_encode(&invalid,packet,sizeof packet);assert(size>0 && !bd_parse(packet,(size_t)size,&old));
    }
    frame.age_ms[BD_REAGENT_LEVEL]=3499;
    assert(bd_is_fresh(&frame,BD_REAGENT_LEVEL,0) && !bd_is_fresh(&frame,BD_REAGENT_LEVEL,1));
    assert(bd_is_fresh(&frame,BD_REAGENT_AVERAGE,1));
    near(bd_display_selected(BD_REAGENT_LEVEL,100,2),100);
    near(bd_display_selected(BD_REAGENT_AVERAGE,1.25,0),1.25);
    assert(!strcmp(bd_unit_selected(BD_REAGENT_LEVEL,0),"%"));
    assert(!strcmp(bd_unit_selected(BD_REAGENT_DEMAND,2),"L/h"));
    assert(bd_bar(BD_REAGENT_AVERAGE,10)==1000 && bd_bar(BD_REAGENT_AVERAGE,327.675)==1000);
    assert(!(bd_diesel_candidates() & (BD_BIT(BD_REAGENT_LEVEL)|BD_BIT(BD_REAGENT_AVERAGE)|BD_BIT(BD_REAGENT_DEMAND))));
    puts("Reagent: independent flags/full raw domains, frozen NBG10, strict NBG11, bounds/freshness/units passed.");
}
