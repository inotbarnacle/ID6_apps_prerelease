#include "gauge_preferences.h"
#include "gauge_core.h"
#include "drive_frame.h"
#include <assert.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static char window_path[128],gas_path[128],n57_path[128];
static bool fail_window_save;
static unsigned old_preference_writes,launches;
static int configured_n57=1;
static uint64_t fixture_now=100000;

static const char* storagePath(const char* path) {
    if (!strcmp(path,"/var/opt/hmi/data/ID5/ShiftDeck.n57.window.prefs")) return window_path;
    if (!strcmp(path,"/var/opt/hmi/data/ID5/ShiftDeck.n57.prefs")) return n57_path;
    if (!strcmp(path,"/var/opt/hmi/data/ID5/ShiftDeck.prefs")) return gas_path;
    assert(false);return path;
}
static FILE* window_fopen(const char* path,const char* mode) { return fopen(storagePath(path),mode); }
static bool window_save(const char* path,const unsigned char* bytes,size_t length) {
    assert(!strcmp(path,"/var/opt/hmi/data/ID5/ShiftDeck.n57.window.prefs"));
    return !fail_window_save && savePersistentBytes(storagePath(path),bytes,length);
}
static bool old_load(const char* path,unsigned limit,unsigned& setting,bool& metric) {
    return loadGaugePreferences(storagePath(path),limit,setting,metric);
}
static void old_save(const char* path,unsigned setting,bool metric) {
    ++old_preference_writes;saveGaugePreferences(storagePath(path),setting,metric);
}

/* Redirect only file/clock boundaries; run the production window and app class. */
#define fopen window_fopen
#define savePersistentBytes window_save
#include "shift_window.h"
#undef fopen
#undef savePersistentBytes
#define loadGaugePreferences old_load
#define saveGaugePreferences old_save
#define bd_mono_ms fixture_mono_ms
static uint64_t fixture_mono_ms() { return fixture_now; }
#define DIAGNOSTIC_WORKER_H
static inline pid_t launch(const char*const*,int) { ++launches;return -1; }
extern "C" int n57_settings(unsigned* index) { *index=0x010203;return configured_n57; }
#include "../src/plugins/shift_deck.cpp"
#undef bd_mono_ms
#undef loadGaugePreferences
#undef saveGaugePreferences

static void tick(ShiftDeck& app) { app.testDeliver(app.testLastMessage(kShiftPoll)); }

static void sendPacket(ShiftDeck& app,int sender,const void* bytes,size_t length) {
    struct sockaddr_in target;memset(&target,0,sizeof target);
    target.sin_family=AF_INET;target.sin_port=htons(DRIVE_PORT);
    target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(sendto(sender,bytes,length,0,reinterpret_cast<struct sockaddr*>(&target),sizeof target)==
           static_cast<ssize_t>(length));
    usleep(1000);tick(app);
}
static void n57(ShiftDeck& app,int sender,int rpm) {
    fixture_now+=100;
    drive_n57_frame frame={DRIVE_N57_MAGIC,fixture_now,rpm,0};
    sendPacket(app,sender,&frame,sizeof frame);
}
static void gasoline(ShiftDeck& app,int sender,int rpm) {
    fixture_now+=100;
    drive_frame frame={fixture_now,rpm,0};sendPacket(app,sender,&frame,sizeof frame);
}
static unsigned property(const ShiftDeck& app,unsigned component,unsigned key) {
    return app.properties.find(std::make_pair(component,key))->second;
}
static void normal(const ShiftDeck& app) {
    assert(app.images.find(57)->second==kShiftOff && app.images.find(55)->second==kLightBase);
#ifdef NATIVE_SHIFT_SMOOTH
    assert(app.images.find(54)->second>=kN57SweepNormal && app.images.find(54)->second<=kN57SweepNormal+128);
#else
    assert(app.images.find(54)->second>=kN57BarBase && app.images.find(54)->second<=kN57BarBase+32);
#endif
}
static void warning(const ShiftDeck& app) {
    assert(app.images.find(57)->second==kN57ShiftWarning && app.images.find(55)->second==kN57LightsWarning);
    assert(app.images.find(60)->second>=kN57DigitWarning && app.images.find(60)->second<kN57DigitWarning+12);
#ifdef NATIVE_SHIFT_SMOOTH
    assert(app.images.find(54)->second>=kN57SweepWarning && app.images.find(54)->second<=kN57SweepWarning+128);
#else
    assert(app.images.find(54)->second>=kN57BarWarning && app.images.find(54)->second<=kN57BarWarning+32);
#endif
}
static void strong(const ShiftDeck& app) {
    assert(app.images.find(57)->second==kShiftOn && app.images.find(55)->second==kLightBase+13);
    assert(app.images.find(60)->second>=kDigitWarn && app.images.find(60)->second<kDigitWarn+12);
}
static void range(const ShiftDeck& app,const char* expected) { assert(app.texts.find(80)->second==expected); }
static ShiftWindow storedWindow() {
    ShiftWindow result;assert(result.load("/var/opt/hmi/data/ID5/ShiftDeck.n57.window.prefs"));return result;
}

/* Replay actual worker packets/receive times supplied by the delayed-MOST Python fixture. */
static void replay(ShiftDeck& app,int sender) {
    unsigned long long sampled,received;
    uint64_t previous=0;
    int rpm,speed;
    unsigned count=0;
    while (scanf("%llu %llu %d %d",&sampled,&received,&rpm,&speed)==4) {
        assert(received>0);
        if (previous) {
            fixture_now=received-1;tick(app);
            const bool live=fixture_now>=previous && fixture_now-previous<750;
            assert((app.images[61]!=kDigitBase+10)==live);
        }
        fixture_now=received;
        drive_n57_frame packet={DRIVE_N57_MAGIC,static_cast<uint64_t>(sampled),rpm,speed};
        sendPacket(app,sender,&packet,sizeof packet);
        const bool live=received>=sampled && received-sampled<750;
        assert((app.images[61]!=kDigitBase+10)==live);
        previous=sampled;++count;
    }
    assert(count>0);
    if (fixture_now<previous+750) fixture_now=previous+750;
    tick(app);normal(app);assert(app.images[61]==kDigitBase+10);
}

int main(int argc,char** argv) {
    assert(DRIVE_STALE_MS==750); /* Timing fixes must not silently extend the shift-alert safety window. */
    char directory[]="/tmp/id6-shift-deck-XXXXXX";assert(mkdtemp(directory));
    snprintf(window_path,sizeof window_path,"%s/window",directory);
    snprintf(gas_path,sizeof gas_path,"%s/gas",directory);
    snprintf(n57_path,sizeof n57_path,"%s/n57",directory);
    saveGaugePreferences(gas_path,3,false);saveGaugePreferences(n57_path,6,true);
    int sender=socket(AF_INET,SOCK_DGRAM,0);assert(sender>=0);
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    ShiftDeck app(dispatcher,bmw::BasicString<char>("ShiftDeck"),1,0,version,bundle,bundle);
    assert(app.onInit());range(app,"4600 - 4800");normal(app);
    assert(!property(app,30,3) && property(app,412,3));
    for (unsigned i=301;i<=308;++i) assert(!property(app,i,3));
    assert(!property(app,202,3) && !property(app,214,3));
    for (unsigned i=250;i<=253;++i) assert(property(app,i,3));
    assert(property(app,250,20)==kN57AxisLeft+(kN57AxisSpan*4600u+3000u)/6000u-3u);
    assert(property(app,251,20)==kN57AxisLeft+(kN57AxisSpan*4800u+3000u)/6000u-3u);
    assert(app.images[66]==kN57Scale);
    app.testEvent(100,true);
    if (argc==2 && !strcmp(argv[1],"--most-replay")) {
        replay(app,sender);app.onFinalize();close(sender);
        unlink(window_path);unlink(gas_path);unlink(n57_path);assert(rmdir(directory)==0);
        puts("ShiftDeck actual class: delayed native MOST packet replay OK");return 0;
    }
    assert(argc==1);
    n57(app,sender,4599);normal(app);assert(launches==1);
    n57(app,sender,4600);warning(app);
    n57(app,sender,4799);warning(app);
    n57(app,sender,4800);strong(app);

    /* Rejected future packets cannot refresh a previous valid alert's TTL. */
    const uint64_t last_valid=fixture_now;
    fixture_now=last_valid+400;
    drive_n57_frame future={DRIVE_N57_MAGIC,fixture_now+5000,6000,0};
    sendPacket(app,sender,&future,sizeof future);strong(app);
    fixture_now=last_valid+DRIVE_STALE_MS;tick(app);normal(app);
#ifdef NATIVE_SHIFT_SMOOTH
    assert(app.images[54]==kN57SweepNormal);
#else
    assert(app.images[54]==kN57BarBase);
#endif
    assert(app.images[60]==kDigitBase+11 && app.images[61]==kDigitBase+10);
    sendPacket(app,sender,&future,sizeof future);normal(app);

    /* Replay the delayed MOST worker cadence at intermediate UI ticks: real read age is retained. */
    const uint64_t sequence=fixture_now+100;
    for (unsigned i=0;i<4;++i) {
        const uint64_t sampled=sequence+i*325;
        fixture_now=sampled+80;
        drive_n57_frame delayed={DRIVE_N57_MAGIC,sampled,4800,0};
        sendPacket(app,sender,&delayed,sizeof delayed);strong(app);
        fixture_now=sampled+400;tick(app);strong(app);
        assert(app.images[60]==kDigitWarn+4 && app.images[61]==kDigitWarn+8);
    }
    const uint64_t final_sample=sequence+3*325;
    fixture_now=final_sample+749;tick(app);strong(app);
    fixture_now=final_sample+750;tick(app);normal(app);
    assert(app.images[60]==kDigitBase+11 && app.images[61]==kDigitBase+10);
    fixture_now+=100;
    drive_n57_frame slow_read={DRIVE_N57_MAGIC,fixture_now-800,4800,0};
    sendPacket(app,sender,&slow_read,sizeof slow_read);normal(app);
    n57(app,sender,4800);strong(app);
    fixture_now+=50;
    drive_n57_frame refused={DRIVE_N57_MAGIC,fixture_now,-1,-1};
    sendPacket(app,sender,&refused,sizeof refused);normal(app);
    assert(app.images[61]==kDigitBase+10);
    n57(app,sender,4700);warning(app);

    /* N57 ignores stale single-threshold actions and keeps metric preferences separate. */
    app.testAction(41);assert(old_preference_writes==0);range(app,"4600 - 4800");
    app.testAction(122);range(app,"4700 - 4800");
    assert(storedWindow().lower==4700 && storedWindow().upper==4800);
    app.testAction(122);range(app,"4700 - 4800");
    assert(app.texts[83].find("Limit reached")!=std::string::npos);
    app.testAction(123);range(app,"4700 - 4800");
    app.testAction(124);range(app,"4700 - 4900");
    app.testAction(125);range(app,"4600 - 4800");
    assert(storedWindow().lower==4600 && storedWindow().upper==4800);
    fail_window_save=true;app.testAction(122);range(app,"4600 - 4800");
    assert(app.texts[83].find("Save failed")!=std::string::npos);
    assert(storedWindow().lower==4600 && storedWindow().upper==4800);
    fail_window_save=false;
    app.testAction(121);app.testAction(124);range(app,"4500 - 4900");
    assert(old_preference_writes==0);
    unsigned old_target=0;bool metric=false;
    assert(loadGaugePreferences(n57_path,8,old_target,metric) && old_target==6 && metric);
    app.testEvent(100,false);normal(app);app.onFinalize();
    assert(app.onInit());range(app,"4500 - 4900");
    app.testEvent(100,true);n57(app,sender,4499);normal(app);
    n57(app,sender,4500);warning(app);n57(app,sender,4900);strong(app);

    /* Profile changes restore the untouched gasoline single-threshold UI and logic. */
    gasoline(app,sender,6500);strong(app);
    assert(property(app,30,3) && !property(app,412,3));
    assert(property(app,202,3) && property(app,214,3));
    for (unsigned i=301;i<=308;++i) assert(property(app,i,3));
    for (unsigned i=250;i<=253;++i) assert(!property(app,i,3));
    assert(app.images[66]==kScale && app.images[56]==kTargetBase+3);
    app.testAction(42);assert(old_preference_writes==1);
    gasoline(app,sender,5999);assert(app.images[57]==kShiftOff);
#ifdef NATIVE_SHIFT_SMOOTH
    assert(app.images[54]>=kSweepBase && app.images[54]<=kSweepBase+128);
#else
    assert(app.images[54]>=kBarBase && app.images[54]<=kBarBase+32);
#endif
    gasoline(app,sender,6000);strong(app);
    app.testAction(125);assert(storedWindow().lower==4500 && storedWindow().upper==4900);
    n57(app,sender,4700);warning(app);range(app,"4500 - 4900");
    app.testEvent(100,false);normal(app);app.onFinalize();

    /* Invalid window preferences never import the old 4000-RPM target. */
    const unsigned char bad[]={'W',2,40,45};assert(savePersistentBytes(window_path,bad,sizeof bad));
    assert(app.onInit());range(app,"4600 - 4800");app.onFinalize();
    close(sender);unlink(window_path);unlink(gas_path);unlink(n57_path);assert(rmdir(directory)==0);
    puts("ShiftDeck actual class: N57 window levels, freshness, settings rollback, persistence and gasoline compatibility OK");
}
