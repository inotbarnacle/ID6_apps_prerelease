#include "fake_bmw.h"
#include "gauge_core.h"
#include "diagnostic_worker.h"
#include "warm_units.h"
#include "dct_settings.h"
#include "surface_protocol.h"
#include <assert.h>
#include <math.h>
#include <poll.h>
#include <sys/wait.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
static uint64_t clock_ms=1000;
static uint64_t test_clock() { return clock_ms; }
static bool units=false;
static bool units_save_ok=true;
static bool test_load_units() { return units; }
static bool test_save_units(bool value) { if (!units_save_ok) return false;units=value;return true; }
static bool test_toggle_dct() { return true; }
static unsigned launch_attempts=0;
static pid_t no_worker(const char* const*,int) { ++launch_attempts;return -1; }
static surface_state captured;
#define ID6_SURFACE_SESSION_H
class SurfaceSession {
public:
    void reap() {}
    void closeChannel() {}
    bool open(const char*) { return true; }
    bool sendState(const surface_state& value,uint64_t) { captured=value;return true; }
};
#define ID6_PLUGIN_NAME "HostWarm"
#define ID6_ENTRY_PAGE 7
#define bd_mono_ms test_clock
#define launch no_worker
#define loadWarmFahrenheit test_load_units
#define saveWarmFahrenheit test_save_units
#define toggleDctTemperature test_toggle_dct
#include "library.cpp"
#undef toggleDctTemperature
#undef saveWarmFahrenheit
#undef loadWarmFahrenheit
#undef launch
#undef bd_mono_ms

static void fill(warm_frame& data,uint64_t time,double oil,double coolant,double trans) {
    for (unsigned i=0;i<WARM_COUNT;++i) data.sample[i]=time;
    data.celsius[0]=oil;data.celsius[1]=coolant;data.celsius[2]=trans;
}
static void poll_app(ID6LibraryLab& app) { bmw::Parcel event;assert(app.invokeDataUpdate(poll_key,event)); }
static void capture(const char* name) {
    const char* directory=getenv("FULL_WARM_CAPTURE_DIR");
    if (!directory) return;
    char path[512];int size=snprintf(path,sizeof path,"%s/%s.bin",directory,name);
    assert(size>0 && static_cast<unsigned>(size)<sizeof path);
    FILE* file=fopen(path,"wb");assert(file);
    assert(fwrite(&captured,1,sizeof captured,file)==sizeof captured && fclose(file)==0);
}
static void send_bytes(ID6LibraryLab& app,int sender,const void* data,size_t size) {
    const sockaddr_in peer=gauge_local_address(port(warm));
    assert(sendto(sender,data,size,0,reinterpret_cast<const sockaddr*>(&peer),sizeof peer)==static_cast<ssize_t>(size));
    poll_app(app);
}
static void pure_state() {
    FullWarmState state;state.reset(1000);
    warm_frame data;fill(data,1000,84.96,75,40);state.accept(data,1000);
    assert(state.target(0)==surface_warm_pending && state.target(1)==surface_warm_met);
    fill(data,1100,85,75,40);state.accept(data,1100);assert(state.eta()==0);
    fill(data,1200,82,73,37);state.accept(data,1200);assert(state.eta()==0);
    fill(data,1300,80.99,69.99,34.99);state.accept(data,1300);
    for (unsigned i=0;i<WARM_COUNT;++i) assert(state.target(i)==surface_warm_pending);
    fill(data,1400,90,75,40);data.sample[0]=0;state.accept(data,1400);
    assert(state.target(0)==surface_warm_unknown && state.target(1)==surface_warm_met && state.target(2)==surface_warm_met);
    data.sample[0]=1200;state.accept(data,1500);assert(state.target(0)==surface_warm_unknown);
    fill(data,1600,0,75,40);state.accept(data,1600);assert(state.target(0)==surface_warm_pending && state.celsius(0)==0);
    const double invalid[]={NAN,INFINITY,-INFINITY,-49,181};
    for (unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        fill(data,1700+i,invalid[i],75,40);state.accept(data,1700+i);
        assert(state.target(0)==surface_warm_unknown && state.target(1)==surface_warm_met);
    }
    state.reset(10000);fill(data,10000,40,47,15);state.accept(data,10000);assert(state.eta()<0);
    fill(data,25000,50,52,20);state.accept(data,25000);assert(state.eta()==69);
    state.update(25000+WARM_STALE_MS);assert(state.valid()==0 && state.eta()<0);
}

int main() {
    pure_state();
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    ID6LibraryLab app(dispatcher,bmw::BasicString<char>("HostWarm"),1,0,version,bundle,bundle);
    assert(app.onInit());app.focusPage(warm,true);
    app.socket_=gauge_bind(port(warm));assert(app.socket_>=0);app.workerPage_=warm;
    const pid_t parent=getpid();pid_t child=fork();assert(child>=0);
    if (!child) { for (unsigned i=0;i<200 && getppid()==parent;++i) ::poll(0,0,50);_exit(0); }
    app.child_=child;
    int sender=gauge_bind(WARM_ACTIVE_PORT);assert(sender>=0);
    int wrong=socket(AF_INET,SOCK_DGRAM,0);assert(wrong>=0);
    warm_frame data;fill(data,clock_ms,84.96,75,40);send_bytes(app,sender,&data,sizeof data);
    assert(!strcmp(captured.values[0],"85.0 C") && captured.boost_signal[0]==surface_warm_pending);
    assert(captured.boost_signal[1]==surface_warm_met && captured.boost_signal[2]==surface_warm_met);
    assert(!strcmp(captured.values[3],"85 C target: pending"));
    capture("rounding-pending");
    bmw::Parcel event;assert(fake_fire(0,5752,event));
    assert(!strcmp(captured.values[0],"184.9 F") && !strcmp(captured.values[3],"185 F target: pending"));
    assert(captured.boost_signal[0]==surface_warm_pending && app.child_==child && units);
    capture("fahrenheit-pending");
    assert(fake_fire(0,5752,event));
    units_save_ok=false;assert(fake_fire(0,5752,event));units_save_ok=true;
    assert(!strcmp(captured.values[0],"85.0 C") && !units);
    clock_ms=2000;fill(data,clock_ms,90,75,40);
    send_bytes(app,wrong,&data,sizeof data);assert(captured.boost_signal[0]==surface_warm_pending);
    char oversized[sizeof data+1];memcpy(oversized,&data,sizeof data);oversized[sizeof data]=0;
    send_bytes(app,sender,oversized,sizeof oversized);assert(captured.boost_signal[0]==surface_warm_pending);
    send_bytes(app,sender,&data,sizeof data-1);assert(captured.boost_signal[0]==surface_warm_pending);
    send_bytes(app,sender,&data,sizeof data);assert(captured.boost_signal[0]==surface_warm_met);
    clock_ms=2500;fill(data,1500,40,40,20);send_bytes(app,sender,&data,sizeof data);
    assert(!strcmp(captured.values[0],"90.0 C"));
    fill(data,2501,40,40,20);send_bytes(app,sender,&data,sizeof data);assert(!strcmp(captured.values[0],"90.0 C"));
    fill(data,clock_ms,NAN,75,40);send_bytes(app,sender,&data,sizeof data);
    assert(captured.boost_signal[0]==surface_warm_unknown && !strcmp(captured.values[0],"--"));
    clock_ms=3000;fill(data,clock_ms,0,75,40);send_bytes(app,sender,&data,sizeof data);
    assert(!strcmp(captured.values[0],"0.0 C") && captured.boost_signal[0]==surface_warm_pending);
    clock_ms=4000;fill(data,clock_ms,0,75,40);data.sample[0]=0;send_bytes(app,sender,&data,sizeof data);
    assert(captured.boost_signal[0]==surface_warm_unknown && captured.boost_signal[1]==surface_warm_met);
    capture("missing-oil");
    data.sample[0]=3000;send_bytes(app,sender,&data,sizeof data);assert(captured.boost_signal[0]==surface_warm_unknown);
    clock_ms=4500;fill(data,clock_ms,85,75,40);send_bytes(app,sender,&data,sizeof data);
    clock_ms+=WARM_STALE_MS;
    data.sample[1]=data.sample[2]=clock_ms;
    send_bytes(app,sender,&data,sizeof data);
    assert(captured.boost_signal[0]==surface_warm_unknown && captured.boost_signal[1]==surface_warm_met);
    assert(captured.boost_signal[2]==surface_warm_met && strstr(captured.status,"all three"));
    clock_ms+=WARM_STALE_MS;poll_app(app);
    for (unsigned i=0;i<3;++i) assert(captured.boost_signal[i]==surface_warm_unknown);
    assert(strstr(captured.status,"Waiting for temperature readings"));
    app.focusPage(warm,false);
    assert(waitpid(child,0,0)==child);
    app.child_=-1; // the stand-in's exit has been observed independently
    clock_ms+=100;app.focusPage(warm,true);
    fill(data,clock_ms-1,85,75,40);
    app.render(reinterpret_cast<const char*>(&data),sizeof data,clock_ms);app.syncSurface();
    assert(captured.boost_signal[1]==surface_warm_unknown); // reject pre-opening positives
    fill(data,clock_ms,85,75,40);app.render(reinterpret_cast<const char*>(&data),sizeof data,clock_ms);
    clock_ms+=100;assert(fake_fire(0,5750,event)); // changing source resets the opening time
    app.render(reinterpret_cast<const char*>(&data),sizeof data,clock_ms);app.syncSurface();
    assert(captured.boost_signal[1]==surface_warm_unknown);
    assert(app.onFinalize() && captured.page==surface_hidden && app.socket_<0);
    close(sender);close(wrong);
    assert(!launch_attempts); // only the stand-in child existed; it never executed diagnostics
    puts("Full-screen Warm production UDP/callbacks: measured states, units, freshness, source guards, ETA and cleanup passed.");
}
