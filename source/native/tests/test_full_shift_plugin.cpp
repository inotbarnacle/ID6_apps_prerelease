#include "fake_bmw.h"
#include "gauge_core.h"
#include "diagnostic_worker.h"
#include "warm_units.h"
#include "surface_protocol.h"
#include <assert.h>
#include <limits.h>
#include <poll.h>
#include <sys/wait.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
static uint64_t clock_ms=1000;
static uint64_t test_clock() { return clock_ms; }
static bool no_units() { return false; }
static unsigned launches=0;
static pid_t no_worker(const char* const*,int) { ++launches;return -1; }
static surface_state captured;
#define ID6_SURFACE_SESSION_H
class SurfaceSession {
public:
    void reap() {} void closeChannel() {} bool open(const char*) { return true; }
    bool sendState(const surface_state& value,uint64_t) { captured=value;return true; }
};
#define ID6_PLUGIN_NAME "HostShift"
#define ID6_ENTRY_PAGE 1
#define bd_mono_ms test_clock
#define launch no_worker
#define loadWarmFahrenheit no_units
#include "library.cpp"
#undef loadWarmFahrenheit
#undef launch
#undef bd_mono_ms

static void poll_app(ID6LibraryLab& app) { bmw::Parcel event;assert(app.invokeDataUpdate(poll_key,event)); }
static void send_bytes(ID6LibraryLab& app,int sender,const void* bytes,size_t length) {
    const sockaddr_in peer=gauge_local_address(port(shift));
    assert(sendto(sender,bytes,length,0,reinterpret_cast<const sockaddr*>(&peer),sizeof peer)==static_cast<ssize_t>(length));
    poll_app(app);
}
static void same(const char* path,const unsigned char* expected,size_t length) {
    unsigned char bytes[16];FILE* file=fopen(path,"rb");assert(file);
    assert(fread(bytes,1,sizeof bytes,file)==length && !ferror(file));
    assert(fclose(file)==0 && !memcmp(bytes,expected,length));
}
static std::string text_model(unsigned model) {
    for (size_t i=fake_updates.size();i;--i)
        if (fake_updates[i-1].model==model) return fake_updates[i-1].text;
    assert(false);return "";
}
static void capture(const char* name) {
    const char* directory=getenv("FULL_SHIFT_CAPTURE_DIR");if (!directory) return;
    char path[512];int size=snprintf(path,sizeof path,"%s/%s.bin",directory,name);
    assert(size>0 && static_cast<unsigned>(size)<sizeof path);
    FILE* file=fopen(path,"wb");assert(file);
    assert(fwrite(&captured,1,sizeof captured,file)==sizeof captured && fclose(file)==0);
}

int main() {
    char directory[]="/tmp/full-shift-plugin.XXXXXX";assert(mkdtemp(directory));
    char gasoline[128],profile[128],missing[128];
    snprintf(gasoline,sizeof gasoline,"%s/gasoline",directory);
    snprintf(profile,sizeof profile,"%s/profile",directory);
    snprintf(missing,sizeof missing,"%s/missing/profile",directory);
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    ID6LibraryLab app(dispatcher,bmw::BasicString<char>("HostShift"),1,0,version,bundle,bundle,gasoline,profile);
    assert(app.onInit());assert(access(gasoline,F_OK)!=0 && access(profile,F_OK)!=0);
    app.focusPage(shift,true);
    app.socket_=gauge_bind(port(shift));assert(app.socket_>=0);app.workerPage_=shift;
    const pid_t parent=getpid();pid_t child=fork();assert(child>=0);
    if (!child) { for (unsigned i=0;i<400 && getppid()==parent;++i) ::poll(0,0,50);_exit(0); }
    app.child_=child;
    int sender=gauge_bind(SHIFT_ACTIVE_PORT);assert(sender>=0);
    int wrong=socket(AF_INET,SOCK_DGRAM,0);assert(wrong>=0);
    drive_frame frame={clock_ms,3999,100,clock_ms};
    send_bytes(app,sender,&frame,sizeof frame);
    assert(captured.magic==surface_magic && captured.boost_level[1]==4000);
    assert(captured.shift_target==6500 && captured.boost_level[0]==8000 && !captured.boost_signal[0]);
    assert(!strcmp(captured.values[0],"100 km/h") && !strcmp(captured.values[1],"3999 rpm"));
    assert(!strcmp(captured.values[2],"--"));
    bmw::Parcel event;
    assert(fake_fire(0,5801,event));
    assert(captured.boost_signal[0]==1 && captured.shift_target==4000 && captured.boost_level[0]==6000);
    assert(access(gasoline,F_OK)!=0);
    const unsigned char initial[]={'S',2,1,30,2};same(profile,initial,sizeof initial);
    capture("diesel-below-target");
    assert(fake_fire(0,5751,event));
    assert(!strcmp(captured.values[0],"62 mph") && app.child_==child);
    const unsigned char original[]={'G',1,5,1};same(gasoline,original,sizeof original);same(profile,initial,sizeof initial);
    assert(fake_fire(0,5810,event));
    assert(captured.shift_target==3900 && captured.boost_level[0]==4000);
    for (unsigned i=0;i<60;++i) assert(fake_fire(0,5820,event));
    assert(captured.shift_target==1000);
    for (unsigned i=0;i<60;++i) assert(fake_fire(0,5821,event));
    assert(captured.shift_target==3900);same(gasoline,original,sizeof original);
    assert(fake_fire(0,5811,event));assert(captured.boost_level[0]==5000);
    for (unsigned i=0;i<20;++i) assert(fake_fire(0,5821,event));
    assert(captured.shift_target==4900);
    assert(fake_fire(0,5812,event));assert(captured.boost_level[0]==6000);
    for (unsigned target=0;target<21;++target) {
        assert(fake_fire(0,5700+target,event));
        assert(!captured.boost_signal[0] && captured.shift_target==6000+target*100 && captured.boost_level[0]==8000);
        const unsigned char encoded[]={'G',1,static_cast<unsigned char>(target),1};same(gasoline,encoded,sizeof encoded);
        FullShiftPreferences restarted;assert(restarted.load(gasoline,profile));
        assert(!restarted.diesel && !restarted.metric && restarted.targetRpm()==6000+target*100);
    }
    assert(fake_fire(0,5801,event));assert(captured.boost_signal[0]);
    app.shiftProfile_=missing;assert(fake_fire(0,5700,event));
    assert(captured.boost_signal[0] && captured.shift_target==4900);
    assert(captured.boost_signal[1]==1);
    assert(text_model(1886).find("partially updated")!=std::string::npos);
    const unsigned char partial[]={'G',1,0,1};same(gasoline,partial,sizeof partial);
    app.shiftProfile_=profile;assert(fake_fire(0,5705,event));
    assert(!captured.boost_signal[0] && captured.shift_target==6500);
    assert(captured.boost_signal[1]==0);
    capture("gasoline-live");
    app.shiftPreferences_=missing;assert(fake_fire(0,5751,event));
    assert(!app.shiftPrefs_.metric && !strcmp(captured.values[0],"62 mph"));
    assert(captured.boost_signal[1]==1 && text_model(1881)=="Could not save speed units");
    app.shiftPreferences_=gasoline;
    assert(fake_fire(0,5801,event));
    clock_ms=1100;frame.sampled_ms=frame.speed_sampled_ms=clock_ms;frame.rpm=4000;
    send_bytes(app,wrong,&frame,sizeof frame);assert(captured.boost_level[1]==4000);
    send_bytes(app,sender,&frame,16);assert(captured.boost_level[1]==4000); // old worker frame
    char oversized[sizeof frame+1];memcpy(oversized,&frame,sizeof frame);oversized[sizeof frame]=0;
    send_bytes(app,sender,oversized,sizeof oversized);assert(captured.boost_level[1]==4000);
    send_bytes(app,sender,&frame,sizeof frame);assert(captured.boost_level[1]==4001);
    frame.sampled_ms=1099;frame.rpm=5000;send_bytes(app,sender,&frame,sizeof frame);assert(captured.boost_level[1]==4001);
    frame.sampled_ms=1101;send_bytes(app,sender,&frame,sizeof frame);assert(captured.boost_level[1]==4001);
    clock_ms=1800;frame.sampled_ms=clock_ms;frame.rpm=0;frame.speed_sampled_ms=1100;
    send_bytes(app,sender,&frame,sizeof frame);assert(captured.boost_level[1]==1);
    clock_ms=1850;poll_app(app);
    assert(captured.boost_level[1]==1 && !strcmp(captured.values[0],"--")); // speed expires independently
    clock_ms=2550;poll_app(app);assert(!captured.boost_level[1]);
    frame.sampled_ms=frame.speed_sampled_ms=clock_ms;frame.rpm=INT_MAX;frame.kph=256;
    send_bytes(app,sender,&frame,sizeof frame);assert(!captured.boost_level[1] && !strcmp(captured.values[0],"--"));
    clock_ms=2600;frame.sampled_ms=frame.speed_sampled_ms=clock_ms;frame.rpm=4500;frame.kph=80;
    send_bytes(app,sender,&frame,sizeof frame);capture("diesel-live");
    frame.sampled_ms=0;send_bytes(app,sender,&frame,sizeof frame);assert(!captured.boost_level[1]);
    frame.sampled_ms=2600;send_bytes(app,sender,&frame,sizeof frame);assert(!captured.boost_level[1]);
    app.focusPage(shift,false);assert(waitpid(child,0,0)==child);app.child_=-1;
    clock_ms=2700;app.focusPage(shift,true);
    frame.sampled_ms=frame.speed_sampled_ms=clock_ms-1;
    app.render(reinterpret_cast<const char*>(&frame),sizeof frame,clock_ms);app.syncSurface();
    assert(!captured.boost_level[1]);
    assert(app.onFinalize() && captured.page==surface_hidden && app.socket_<0);
    close(sender);close(wrong);unlink(gasoline);unlink(profile);assert(rmdir(directory)==0);
    assert(!launches);
    puts("Full-screen Shift production UDP/callbacks: 21 gasoline presets, diesel settings, persistence failures, raw/independent freshness and cleanup passed.");
}
