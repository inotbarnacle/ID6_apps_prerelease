#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#ifdef __GLIBC__
static unsigned read_calls,close_calls;
static ssize_t failed_read(void*,char*,size_t) { ++read_calls;errno=EIO;return -1; }
static int counted_close(void*) { ++close_calls;return 0; }
#endif
static FILE* preference_open(const char* path,const char* mode) {
#ifdef __GLIBC__
    if (!strcmp(path,"test-error-stream")) {
        cookie_io_functions_t hooks={0,0,0,0};hooks.read=failed_read;hooks.close=counted_close;
        FILE* file=fopencookie(0,"r",hooks);assert(file);setvbuf(file,0,_IONBF,0);return file;
    }
#endif
    return fopen(path,mode);
}
#define fopen preference_open
#include "boost_layout.h"
#include "boost_state.h"
#undef fopen

static void same(const char* path,const unsigned char* expected,size_t size) {
    unsigned char data[64];FILE* file=fopen(path,"rb");assert(file);
    assert(fread(data,1,sizeof data,file)==size && !ferror(file));
    assert(fclose(file)==0 && !memcmp(data,expected,size));
}

int main() {
    assert(BD_COUNT==58 && sizeof(bd_mask)==8);
    char directory[]="/tmp/full-boost-state.XXXXXX";assert(mkdtemp(directory));
    char layout_path[128],mode_path[128];
    snprintf(layout_path,sizeof layout_path,"%s/layout",directory);
    snprintf(mode_path,sizeof mode_path,"%s/mode",directory);
    for (unsigned version=1;version<=2;++version) {
        unsigned char old[25]={'B',static_cast<unsigned char>(version),1};
        const unsigned stride=version==1 ? 11 : 9;
        for (unsigned profile=0;profile<2;++profile) for (unsigned i=0;i<stride;++i)
            old[3+profile*stride+i]=static_cast<unsigned char>((i+profile)%31);
        assert(savePersistentBytes(layout_path,old,3+stride*2));
        BoostLayout loaded;assert(loaded.load(layout_path) && loaded.diesel);
        for (unsigned profile=0;profile<2;++profile) for (unsigned i=0;i<9;++i)
            assert(loaded.signal[profile][i]==(i+profile)%31);
        same(layout_path,old,3+stride*2); // load never rewrites the source
        old[3]=31;assert(savePersistentBytes(layout_path,old,3+stride*2));
        assert(!loaded.load(layout_path)); // wider build must not reinterpret old IDs
    }
    for (unsigned signal=0;signal<BD_COUNT;++signal) {
        BoostLayout saved;saved.diesel=true;saved.signal[1][8]=static_cast<unsigned char>(signal);
        assert(saved.save(layout_path));
        BoostLayout loaded;assert(loaded.load(layout_path) && loaded.diesel && loaded.signal[1][8]==signal);
        FullBoostDefaultsMode mode;mode.automatic=false;assert(mode.save(mode_path));
        const unsigned char record[]={'D',1,0};same(mode_path,record,sizeof record);
        mode.automatic=true;mode.load(mode_path);assert(!mode.automatic);
        unsigned char bytes[25];size_t length=0;FILE* file=fopen(layout_path,"rb");assert(file);
        length=fread(bytes,1,sizeof bytes,file);assert(fclose(file)==0 && length==21);
        const unsigned maximum=signal>18 ? signal : 18;
        assert(bytes[1]==(maximum>=55 ? 6 : maximum>=47 ? 5 : maximum>=39 ? 4 : maximum>=31 ? 3 : 2));
        same(layout_path,bytes,length);
    }
#ifdef __GLIBC__
    for (unsigned reader=0;reader<3;++reader) {
        read_calls=close_calls=0;
        if (reader==0) { BoostLayout loaded;assert(!loaded.load("test-error-stream")); }
        if (reader==1) { BoostPreferences loaded;loaded.load("test-error-stream",9); }
        if (reader==2) { FullBoostDefaultsMode loaded;loaded.load("test-error-stream"); }
        assert(read_calls==1 && close_calls==1); // the previous read-after-error paths call twice
    }
#endif
    unlink(layout_path);unlink(mode_path);assert(rmdir(directory)==0);

    FullBoostState state;state.reset(1000);
    bd_frame frame;memset(&frame,0,sizeof frame);frame.sent_ms=1000;
    frame.valid=BD_BIT(BD_BOOST)|BD_BIT(BD_RPM)|BD_BIT(BD_COOLANT)|BD_BIT(BD_REAGENT_DEMAND);
    frame.value[BD_RPM]=0;frame.value[BD_COOLANT]=75;frame.value[BD_REAGENT_DEMAND]=0.125;
    char packet[BD_AUTO_PACKET];int size=bd_encode(&frame,packet,sizeof packet);assert(size>0);
    assert(!memcmp(packet,"NBG11",5));
    assert(state.accept(packet,size,1000,false,0,-1) && state.fresh(BD_RPM,1000));
    assert(state.frame().value[BD_RPM]==0 && state.frame().value[BD_REAGENT_DEMAND]==0.125);
    assert(!state.fresh(BD_BOOST,3500) && state.fresh(BD_COOLANT,4499) && !state.fresh(BD_COOLANT,4500));
    assert(!state.fresh(BD_EMPTY_SLOT,1000));
    frame.sent_ms=999;size=bd_encode(&frame,packet,sizeof packet);assert(!state.accept(packet,size,1100,false,0,-1));
    frame.sent_ms=1101;size=bd_encode(&frame,packet,sizeof packet);assert(!state.accept(packet,size,1100,false,0,-1));
    frame.sent_ms=1100;frame.demo=1;size=bd_encode(&frame,packet,sizeof packet);assert(!state.accept(packet,size,1100,false,0,-1));
    frame.demo=0;frame.sent_ms=1200;
    bd_cap_frame report={42,1200,123,BD_BIT(BD_RPM)|BD_BIT(BD_COOLANT)|BD_BIT(BD_EGT_B1S2),BD_BIT(BD_RPM)|BD_BIT(BD_COOLANT)};
    state.reset(1200);size=bd_auto_encode(&report,&frame,packet,sizeof packet);assert(size>0);
    assert(!state.accept(packet,size,1200,true,41,123));
    assert(!state.accept(packet,size,1200,true,42,124));
    assert(state.accept(packet,size,1200,true,42,123));
    assert(state.automaticSignal(0)==BD_COOLANT && state.automaticSignal(8)==BD_RPM);
    assert(state.automaticSignal(1)==BD_EMPTY_SLOT);
    report.supported|=BD_BIT(BD_EGT_B1S2);report.sent_ms=frame.sent_ms=1300;
    size=bd_auto_encode(&report,&frame,packet,sizeof packet);assert(size>0);
    assert(!state.accept(packet,size,1300,true,42,123)); // known evidence cannot flip
    state.reset(1300);assert(state.accept(packet,size,1300,true,42,123));
    assert(state.automaticSignal(1)==BD_EGT_B1S2);
    assert(!state.accept(packet,size,1300+BD_STALE_MS,true,42,123));
    state.reset(1400);assert(!state.accept(packet,size,1400,true,42,123));
    puts("Full-screen Boost: 58 IDs, B1-B6 files, independent mode, real read errors, 64-bit data, freshness and capability generation checks passed.");
}
