#include "dpf_core.h"
#include "fixtures/dpf_v1_frame.h"
#include <assert.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void near(double a,double b) { assert(fabs(a-b)<1e-9); }

static void decoding(void) {
    struct dpf_sample s,before;
    const unsigned char b1[]={7,0xff,0xf6,0,0,0x27,0x10};
    const unsigned char b2[]={5,0x80,0,0xff,0xff,0xff,0xff};
    const unsigned char temp[]={15,1,0x90,1,0x91,0x47,0xe0,0xff,0xff};
    assert(dpf_decode(0x7a,b1,sizeof b1,&s));
    assert(s.known==7 && s.supported==7 && s.valid==7);
    near(s.value[DPF_B1_DELTA],-.1);near(s.value[DPF_B1_INLET_PRESSURE],0);near(s.value[DPF_B1_OUTLET_PRESSURE],100);
    assert(dpf_decode(0x7b,b2,sizeof b2,&s));
    assert(s.known==(7u<<3) && s.supported==(5u<<3) && s.valid==s.supported);
    near(s.value[DPF_B2_DELTA],-327.68);near(s.value[DPF_B2_OUTLET_PRESSURE],655.35);
    assert(s.value[DPF_B2_INLET_PRESSURE]==0); /* mask, not this zero, decides availability */
    assert(dpf_decode(0x7c,temp,sizeof temp,&s));
    assert(s.known==(15u<<6) && s.supported==(15u<<6) && s.valid==(7u<<6));
    near(s.value[DPF_B1_INLET_TEMP],0);near(s.value[DPF_B1_OUTLET_TEMP],.1);near(s.value[DPF_B2_INLET_TEMP],1800);
    assert(!(s.valid & DPF_BIT(DPF_B2_OUTLET_TEMP))); /* app envelope, not a FFFF sentinel */
    for (unsigned raw=0;raw<256;++raw) {
        const unsigned char p=(unsigned char)raw;
        assert(dpf_decode(0x05,&p,1,&s));
        assert(s.known==DPF_BIT(DPF_COOLANT) && s.valid==s.known);
        near(s.value[DPF_COOLANT],(int)raw-40);
    }
    memset(&s,0xa5,sizeof s);before=s;
    for (size_t size=0;size<12;++size) {
        unsigned char p[12]={0};
        if (size!=7) { assert(!dpf_decode(0x7a,p,size,&s));assert(!memcmp(&s,&before,sizeof s)); }
        if (size!=9) { assert(!dpf_decode(0x7c,p,size,&s));assert(!memcmp(&s,&before,sizeof s)); }
        if (size!=1) { assert(!dpf_decode(0x05,p,size,&s));assert(!memcmp(&s,&before,sizeof s)); }
    }
    for (unsigned pid=0;pid<256;++pid)
        if (pid!=5 && pid!=0x7a && pid!=0x7b && pid!=0x7c && pid!=0x8b) assert(!dpf_decode(pid,b1,sizeof b1,&s));
    assert(!dpf_decode(0x7a,0,7,&s) && !dpf_decode(0x7a,b1,7,0));
}

static void flags_and_domains(void) {
    struct dpf_sample s;
    for (unsigned pid=0x7a;pid<=0x7c;++pid) {
        const unsigned count=pid==0x7c ? 4 : 3,first=pid==0x7c ? 6 : pid==0x7b ? 3 : 0;
        for (unsigned flags=0;flags<256;++flags) {
            unsigned char p[9]={0};
            p[0]=(unsigned char)flags;
            for (unsigned i=0;i<count;++i) { p[1+2*i]=1;p[2+2*i]=(unsigned char)(144+i); }
            memset(&s,0xa5,sizeof s);
            const struct dpf_sample before=s;
            assert(dpf_decode(pid,p,1+2*count,&s)==(flags<(1u<<count)));
            if (flags>=(1u<<count)) { assert(!memcmp(&s,&before,sizeof s));continue; }
            assert(s.known==dpf_pid_mask(pid) && s.supported==(flags<<first) && s.valid==s.supported);
        }
    }
    for (unsigned raw=0;raw<65536;++raw) {
        unsigned char p[9]={7,0,0,0,0,0,0,0,0};
        for (unsigned i=0;i<4;++i) { p[1+2*i]=(unsigned char)(raw>>8);p[2+2*i]=(unsigned char)raw; }
        for (unsigned pid=0x7a;pid<=0x7b;++pid) {
            const unsigned first=pid==0x7a ? 0 : 3;
            assert(dpf_decode(pid,p,7,&s));
            near(s.value[first],(raw>=32768 ? (int)raw-65536 : (int)raw)/100.0);
            near(s.value[first+1],raw/100.0);near(s.value[first+2],raw/100.0);
        }
        p[0]=15;
        assert(dpf_decode(0x7c,p,9,&s));
        assert(s.supported==(15u<<6));
        assert(s.valid==(raw<=18400 ? 15u<<6 : 0));
        if (raw<=18400)
            for (unsigned i=6;i<10;++i) near(s.value[i],((int)raw-400)/10.0);
    }
}

static struct dpf_frame sample(void) {
    struct dpf_frame f;
    memset(&f,0,sizeof f);
    f.seq=1;f.worker_pid=123;f.token=42;f.sent_ms=1000;
    f.known=f.supported=f.valid=DPF_ALL;
    for (unsigned i=0;i<DPF_COUNT;++i) { f.value[i]=i+.25;f.age_ms[i]=i*10; }
    f.value[DPF_B1_DELTA]=-.01;f.value[DPF_B2_DELTA]=-327.68;
    f.value[DPF_B1_INLET_TEMP]=0;
    f.value[DPF_REGEN_PROGRESS]=f.value[DPF_REGEN_TYPE]=1;
    return f;
}

static void reject(const char *data,size_t size) {
    struct dpf_frame out,before;
    memset(&out,0x5a,sizeof out);before=out;
    assert(!dpf_parse(data,size,&out) && !memcmp(&out,&before,sizeof out));
}

static void replace_word(const char *packet,unsigned word,const char *value) {
    char changed[1024];
    const char *start=packet,*end;
    for (unsigned i=0;i<word;++i) { start=strchr(start,' ');assert(start);++start; }
    end=start;
    while (*end && *end!=' ' && *end!='\n') ++end;
    int size=snprintf(changed,sizeof changed,"%.*s%s%s",(int)(start-packet),packet,value,end);
    assert(size>0 && (size_t)size<sizeof changed);
    reject(changed,(size_t)size);
}

static void packets(void) {
    struct dpf_frame f=sample(),out;
    char packet[DPF_MAX_PACKET],changed[DPF_MAX_PACKET+1];
    const int size=dpf_encode(&f,packet,sizeof packet);
    assert(size>0 && size<DPF_MAX_PACKET && packet[size-1]=='\n');
    assert(dpf_parse(packet,(size_t)size,&out));
    assert(out.seq==1 && out.worker_pid==123 && out.token==42 && out.sent_ms==1000 && out.valid==DPF_ALL);
    for (unsigned i=0;i<DPF_COUNT;++i) { near(out.value[i],f.value[i]);assert(out.age_ms[i]==f.age_ms[i]); }
    for (unsigned bound=0;bound<=(unsigned)size;++bound) assert(dpf_encode(&f,changed,bound)<0);
    for (size_t length=0;length<(size_t)size;++length) reject(packet,length);
    memcpy(changed,packet,(size_t)size);changed[size]=0;
    changed[1]='X';reject(changed,(size_t)size);
    memcpy(changed,packet,(size_t)size);changed[5]=0;reject(changed,(size_t)size);
    memcpy(changed,packet,(size_t)size);changed[7]='\n';reject(changed,(size_t)size);
    memcpy(changed,packet,(size_t)size);changed[size]='\n';reject(changed,(size_t)size+1);
    memset(changed,' ',sizeof changed);changed[DPF_MAX_PACKET-1]='\n';reject(changed,DPF_MAX_PACKET);
    replace_word(packet,0,"NBG8");replace_word(packet,0,"NBGA");replace_word(packet,0,"DPF10");
    replace_word(packet,1,"0");replace_word(packet,1,"4294967296");
    replace_word(packet,2,"0");replace_word(packet,2,"2147483648");
    replace_word(packet,3,"0");replace_word(packet,3,"18446744073709551616");
    replace_word(packet,4,"0");replace_word(packet,5,"8192");
    replace_word(packet,5,"0");replace_word(packet,6,"0");
    replace_word(packet,8,"-327.69");replace_word(packet,8,"327.68");
    replace_word(packet,10,"-0.01");replace_word(packet,10,"655.36");
    replace_word(packet,20,"-40.01");replace_word(packet,20,"1800.01");
    replace_word(packet,28,"215.01");replace_word(packet,9,"1000000");
    replace_word(packet,30,"0.5");replace_word(packet,30,"2");
    replace_word(packet,32,"-1");replace_word(packet,32,"1.01");
    replace_word(packet,0,"DPF1"); /* thirteen fields must not be relabeled as DPF1 */
    for (unsigned i=0;i<11;++i) {
        static const char *bad[]={"nan","inf","-inf","1e0","0x1p0","1.","+1","--1","1\t2","0.001","0,01"};
        replace_word(packet,8,bad[i]);
    }
    f.seq=UINT32_MAX;f.worker_pid=INT_MAX;f.token=f.sent_ms=UINT64_MAX;
    assert(dpf_encode(&f,packet,sizeof packet)>0 && dpf_parse(packet,strlen(packet),&out));
    assert(out.seq==f.seq && out.token==f.token && out.sent_ms==f.sent_ms);
    for (unsigned bit=0;bit<DPF_COUNT;++bit) {
        f=sample();f.valid&=~DPF_BIT(bit);f.value[bit]=NAN;f.age_ms[bit]=UINT_MAX;
        assert(dpf_encode(&f,packet,sizeof packet)>0 && dpf_parse(packet,strlen(packet),&out));
        assert(out.value[bit]==0 && out.age_ms[bit]==0 && !(out.valid & DPF_BIT(bit)));
        if (bit==0) { replace_word(packet,8,"0.01");replace_word(packet,9,"1"); }
    }
    f=sample();f.value[0]=NAN;assert(dpf_encode(&f,packet,sizeof packet)<0);
    assert(!dpf_parse(packet,1,0) && !dpf_parse(0,1,&out));
}

static void availability_and_units(void) {
    struct dpf_frame f=sample();
    assert(dpf_measurement_state(&f,0,0)==DPF_VALID);
    assert(dpf_measurement_state(&f,0,DPF_STALE_MS-1)==DPF_VALID);
    assert(dpf_measurement_state(&f,0,DPF_STALE_MS)==DPF_STALE);
    assert(dpf_measurement_state(&f,0,UINT64_MAX)==DPF_STALE);
    f.age_ms[0]=DPF_STALE_MS-1;
    assert(dpf_measurement_state(&f,0,0)==DPF_VALID && dpf_measurement_state(&f,0,1)==DPF_STALE);
    f.valid&=~1u;assert(dpf_measurement_state(&f,0,0)==DPF_UNAVAILABLE);
    f.supported&=~1u;assert(dpf_measurement_state(&f,0,0)==DPF_UNSUPPORTED);
    f.known&=~1u;assert(dpf_measurement_state(&f,0,0)==DPF_UNKNOWN);
    assert(dpf_measurement_state(0,0,0)==DPF_UNKNOWN && dpf_measurement_state(&f,DPF_COUNT,0)==DPF_UNKNOWN);
    near(dpf_display_value(DPF_B1_DELTA,-.01,1),-.1);
    near(dpf_display_value(DPF_B1_INLET_PRESSURE,100,0),14.5037738);
    near(dpf_display_value(DPF_B1_INLET_TEMP,0,0),32);
    near(dpf_display_value(DPF_COOLANT,37,0),98.6);
    assert(!strcmp(dpf_unit(0,1),"mbar") && !strcmp(dpf_unit(0,0),"psi"));
    assert(!strcmp(dpf_unit(6,1),"C") && !strcmp(dpf_unit(10,0),"F"));
    assert(!strcmp(dpf_unit(DPF_REGEN_PROGRESS,0),""));
    near(dpf_display_value(DPF_REGEN_TYPE,1,0),1);
    assert(!strcmp(dpf_unit(DPF_COUNT,1),"") && isnan(dpf_display_value(DPF_COUNT,0,1)));
}

static void regeneration(void) {
    struct dpf_sample decoded,before;
    const unsigned mask=DPF_BIT(DPF_REGEN_PROGRESS)|DPF_BIT(DPF_REGEN_TYPE);
    for (unsigned flags=0;flags<256;++flags)
        for (unsigned status=0;status<256;++status) {
            unsigned char data[]={flags,status,255,255,255,255,255};
            memset(&decoded,0xa5,sizeof decoded);before=decoded;
            const int valid=flags<128 && status<16;
            assert(dpf_decode(0x8b,data,sizeof data,&decoded)==valid);
            if (!valid) { assert(!memcmp(&decoded,&before,sizeof decoded));continue; }
            assert(decoded.known==mask && decoded.supported==((flags&3)<<DPF_REGEN_PROGRESS));
            assert(decoded.valid==decoded.supported);
            for (unsigned i=0;i<2;++i)
                assert(decoded.value[DPF_REGEN_PROGRESS+i]==((flags & (1u<<i)) ? (status>>i)&1 : 0));
        }
    memset(&decoded,0xa5,sizeof decoded);before=decoded;
    for (unsigned size=0;size<10;++size) {
        const unsigned char data[10]={3,3};
        if (size==7) continue;
        assert(!dpf_decode(0x8b,data,size,&decoded) && !memcmp(&decoded,&before,sizeof decoded));
    }
    struct dpf_frame frame=sample();
    frame.age_ms[DPF_REGEN_PROGRESS]=DPF_STALE_MS-1;
    frame.age_ms[DPF_REGEN_TYPE]=0;
    assert(dpf_measurement_state(&frame,DPF_REGEN_PROGRESS,1)==DPF_STALE);
    assert(dpf_measurement_state(&frame,DPF_REGEN_TYPE,1)==DPF_VALID);
}

static void frozen_v1(void) {
    struct dpf_frame frame,again;
    char packet[DPF_MAX_PACKET];
    assert(dpf_parse(dpf_v1_frame,sizeof dpf_v1_frame-1,&frame));
    assert(frame.seq==UINT32_MAX && frame.worker_pid==12345 && frame.token==UINT64_MAX && frame.sent_ms==123456789);
    assert(frame.known==DPF_LEGACY_ALL && frame.supported==DPF_LEGACY_ALL && frame.valid==DPF_LEGACY_ALL);
    const double expected[]={-327.68,0,655.35,327.67,.01,123.45,-40,0,1800,900,215};
    for (unsigned i=0;i<DPF_LEGACY_COUNT;++i) { near(frame.value[i],expected[i]);assert(frame.age_ms[i]==17*i); }
    assert(dpf_measurement_state(&frame,DPF_REGEN_PROGRESS,0)==DPF_UNKNOWN);
    assert(dpf_measurement_state(&frame,DPF_REGEN_TYPE,0)==DPF_UNKNOWN);
    replace_word(dpf_v1_frame,5,"2048");
    assert(dpf_encode(&frame,packet,sizeof packet)>0 && !memcmp(packet,"DPF2 ",5));
    assert(dpf_parse(packet,strlen(packet),&again) && again.valid==DPF_LEGACY_ALL);
}

int main(void) {
    decoding();flags_and_domains();packets();availability_and_units();regeneration();frozen_v1();
    {
        static const char *locales[]={"de_DE.UTF-8","cs_CZ.UTF-8","fr_FR.UTF-8"};
        unsigned checked=0;
        for (unsigned i=0;i<sizeof locales/sizeof locales[0];++i)
            if (setlocale(LC_NUMERIC,locales[i])) {
                if (strcmp(localeconv()->decimal_point,".")) ++checked;
                packets();
            }
        if (getenv("DPF_REQUIRE_COMMA_LOCALE")) assert(checked);
        assert(setlocale(LC_NUMERIC,"C"));
        printf("DPF non-dot numeric locales checked: %u\n",checked);
    }
    puts("DPF: raw domains, independent regeneration flags, frozen DPF1/strict DPF2, freshness and units OK");
    return 0;
}
