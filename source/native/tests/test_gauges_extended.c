#include "gauge_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void label(unsigned id, double value, int metric, const char *expected) {
    char text[128];
    bd_format_value(id,value,metric,text,sizeof text);
    assert(!strcmp(text,expected));
}

static int legacy(const char *version, unsigned count, unsigned long long mask, bd_frame *f) {
    char text[BD_MAX_PACKET];
    unsigned i;
    int n=snprintf(text,sizeof text,"%s 1 0 %llu 1000",version,mask);
    for (i=0;i<count;i++) n+=snprintf(text+n,sizeof text-(size_t)n," 0 0");
    return bd_parse(text,(size_t)n,f);
}

int main(void) {
    bd_frame f, decoded;
    char packet[BD_MAX_PACKET];
    unsigned i;
    int n;
    /* IDs are persisted as bytes; new selectors must be appended. */
    assert(BD_BOOST==0 && BD_THROTTLE_TARGET==30 && BD_DPF_SOOT==31 && BD_POWER==38 && BD_FUEL_VOLUME==39 && BD_REL_TORQUE_DDE==40 && BD_COUNT==41);
    memset(&f,0,sizeof f);
    f.valid=BD_ALL_SIGNALS; f.seq=42; f.sent_ms=123456789;
    for (i=0;i<BD_COUNT;i++) { f.value[i]=19999.999999; f.age_ms[i]=999999; }
    f.value[BD_DPF_REGEN_DIST]=2000000;
    n=bd_encode(&f,packet,sizeof packet);
    assert(n>768 && n<BD_MAX_PACKET && !memcmp(packet,"NBGA",4));
    assert(bd_parse(packet,(size_t)n,&decoded) && decoded.valid==BD_ALL_SIGNALS);
    assert(decoded.value[BD_DPF_REGEN_DIST]==2000000 && decoded.age_ms[BD_POWER]==999999);
    assert(!bd_parse(packet,(size_t)(strrchr(packet,' ')-packet),&decoded));
    assert(legacy("NBG7",31,BD_BIT(30),&decoded) && decoded.valid==BD_BIT(30));
    assert(decoded.value[BD_POWER]==0 && !(decoded.valid & BD_BIT(BD_POWER)));
    assert(!legacy("NBG7",31,BD_BIT(31),&decoded));
    assert(!legacy("NBG8",39,BD_BIT(39),&decoded));
    assert(legacy("NBG8",39,BD_BIT(38),&decoded) && !(decoded.valid & BD_BIT(BD_FUEL_VOLUME)));
    assert(!legacy("NBG9",40,BD_BIT(40),&decoded));
    assert(legacy("NBG9",40,BD_BIT(39),&decoded) && !(decoded.valid & BD_BIT(BD_REL_TORQUE_DDE)));
    assert(!legacy("NBGA",40,BD_BIT(39),&decoded));
    assert(!legacy("NBGA",41,BD_BIT(41),&decoded));
    assert(!legacy("NBG8",39,UINT64_MAX,&decoded));
    assert(legacy("NBG6",22,BD_BIT(21),&decoded));
    assert(legacy("NBG5",11,BD_BIT(10),&decoded));
    assert(legacy("NBG4",9,BD_BIT(8),&decoded));
    assert(legacy("NBG3",8,BD_BIT(7),&decoded));
    f.valid=BD_BIT(BD_POWER); f.age_ms[BD_POWER]=2000;
    assert(bd_is_fresh(&f,BD_POWER,499) && !bd_is_fresh(&f,BD_POWER,500));
    assert(!bd_is_fresh(&f,BD_TORQUE,0));
    label(BD_DPF_SOOT,15.259,1,"15.3 g");
    label(BD_DPF_DP,10,1,"10.0 hPa");
    label(BD_DPF_REGEN_DIST,1500000,1,"1500.0 km");
    label(BD_DPF_REGEN_DIST,160934.4,0,"100.0 mi");
    label(BD_EGT_PRE_DPF,450.3,1,"450 \260C");
    label(BD_EGT_PRE_CAT,100,0,"212 \260F");
    label(BD_ADBLUE,76.3,1,"76.3%");
    label(BD_TORQUE,704.404,1,"704 Nm");
    label(BD_POWER,200,1,"200.0 kW");
    label(BD_POWER,.7456998716*300,0,"300.0 hp");
    label(BD_FUEL_VOLUME,49.582,1,"49.6 L");
    label(BD_FUEL_VOLUME,3.785411784,0,"1.0 US gal");
    label(BD_REL_TORQUE_DDE,50.003968,0,"50.0%");
    label(BD_REL_TORQUE_DDE,50.003968,1,"50.0%");
    f.valid=BD_BIT(BD_REL_TORQUE_DDE); f.age_ms[BD_REL_TORQUE_DDE]=2000;
    assert(bd_is_fresh(&f,BD_REL_TORQUE_DDE,499) && !bd_is_fresh(&f,BD_REL_TORQUE_DDE,500));
    assert(fabs(bd_display_value(BD_TORQUE,1.3558179483,0)-1)<1e-9);
    assert(bd_bar(BD_ADBLUE,50)==500 && bd_bar(BD_POWER,-10)==0 && bd_bar(BD_POWER,600)==1000);
    puts("Extended gauges: NBGA masks/length, legacy NBG9, labels/units, stable IDs and freshness OK");
    return 0;
}
