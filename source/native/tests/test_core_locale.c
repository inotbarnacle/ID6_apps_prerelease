#include "gauge_core.h"
#include "fixtures/boost_locale_frame.h"
#include <assert.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void legacy_frame(bd_frame *frame) {
    memset(frame,0,sizeof *frame);
    frame->seq=123; frame->sent_ms=1234567890123ULL; frame->valid=0x7fffffff;
    for (unsigned i=0;i<31;++i) {
        frame->value[i]=(double)i+.125; frame->age_ms[i]=i*17;
    }
    frame->value[0]=-0.0; frame->value[1]=12.3456785; frame->value[2]=-5.1234565;
}

static void frozen_bytes(void) {
    bd_frame input,output;
    char wire[BD_MAX_PACKET];
    legacy_frame(&input);
    int n=bd_encode(&input,wire,sizeof wire);
    assert(n==(int)sizeof boost_locale_frame-1);
    assert(!memcmp(wire,boost_locale_frame,sizeof boost_locale_frame));
    assert(bd_parse(boost_locale_frame,sizeof boost_locale_frame-1,&output));
    assert(output.seq==input.seq && output.valid==input.valid && output.sent_ms==input.sent_ms);
    assert(signbit(output.value[0]));
    for (unsigned i=0;i<31;++i) {
        assert(fabs(output.value[i]-input.value[i])<=.00000051);
        assert(output.age_ms[i]==input.age_ms[i]);
    }
    assert(bd_encode(&input,wire,sizeof boost_locale_frame-1)<0);
    assert(bd_encode(&input,wire,sizeof boost_locale_frame)==n);
    input.value[0]=INFINITY; assert(bd_encode(&input,wire,sizeof wire)<0);
    input.value[0]=NAN; assert(bd_encode(&input,wire,sizeof wire)<0);
}

static int first_value(const char *value,bd_frame *out) {
    char wire[BD_MAX_PACKET];
    int n=snprintf(wire,sizeof wire,"NBG2 1 0 31 1 %s 0 0 0 0 0 0 0 0 0",value);
    assert(n>0 && (size_t)n<sizeof wire);
    return bd_parse(wire,(size_t)n,out);
}

static void numeric_grammar(void) {
    const char *good[]={"0","-0","+1.25",".125","1.","1.25e+2","-5e-3","0x1.8p+3","-0X1.Ap-2"};
    const double expected[]={0,-0.,1.25,.125,1,125,-.005,12,-.40625};
    const char *bad[]={"1,25","1,000.25","1.000,25","1_000","nan","-inf","infinity",
                       "1e9999","1e-9999","1..2","0x1,8p3","0x1.8p3x","1.2junk","+",".",""};
    bd_frame out,before;
    for (unsigned i=0;i<sizeof good/sizeof good[0];++i) {
        assert(first_value(good[i],&out)); assert(out.value[0]==expected[i]);
        if (i==1) assert(signbit(out.value[0]));
    }
    memset(&before,0xa5,sizeof before);
    for (unsigned i=0;i<sizeof bad/sizeof bad[0];++i) {
        out=before; assert(!first_value(bad[i],&out)); assert(!memcmp(&out,&before,sizeof out));
    }
    const unsigned counts[]={5,8,9,11,22,31};
    for (unsigned version=2;version<=7;++version) {
        char wire[BD_MAX_PACKET];
        unsigned count=counts[version-2];
        int n=snprintf(wire,sizeof wire,"NBG%u 1 0 %u 1",version,(1u<<count)-1);
        assert(n>0);
        for (unsigned i=0;i<count;++i) {
            int added=snprintf(wire+n,sizeof wire-(size_t)n," %s %u",good[i%9],i);
            assert(added>0 && (size_t)added<sizeof wire-(size_t)n); n+=added;
        }
        assert(bd_parse(wire,(size_t)n,&out));
        for (unsigned i=0;i<count;++i) {
            assert(out.value[i]==expected[i%9]); assert(out.age_ms[i]==i);
        }
    }
}

static void cross_locale_roundtrip(const char *locale) {
    bd_frame input,output;
    char reference[BD_MAX_PACKET],wire[BD_MAX_PACKET];
    uint32_t random=1;
    legacy_frame(&input);
    for (unsigned i=0;i<4096;++i) {
        random=random*1664525u+1013904223u;
        input.value[0]=((double)random/UINT32_MAX-.5)*40000;
        assert(setlocale(LC_NUMERIC,"C"));
        int expected=bd_encode(&input,reference,sizeof reference); assert(expected>0);
        assert(setlocale(LC_NUMERIC,locale));
        int n=bd_encode(&input,wire,sizeof wire);
        assert(n==expected && !memcmp(wire,reference,(size_t)n+1));
        assert(bd_parse(reference,(size_t)expected,&output));
        assert(fabs(output.value[0]-input.value[0])<=.00000051);
    }
}

int main(int argc,char **argv) {
    const char *requested=argc>1 ? argv[1] : "C";
    char locale[256];
    const char *active=setlocale(LC_NUMERIC,requested);
    if (!active) { fprintf(stderr,"Required test locale unavailable: %s\n",requested); return 2; }
    assert(strlen(active)<sizeof locale); strcpy(locale,active);
    frozen_bytes(); numeric_grammar();
    assert(!strcmp(setlocale(LC_NUMERIC,NULL),locale));
    cross_locale_roundtrip(locale);
    assert(!strcmp(setlocale(LC_NUMERIC,NULL),locale));
    printf("Boost codec: frozen bytes, NBG2-7, grammar and 4096 round trips passed (%s).\n",locale);
    return 0;
}
