#include "fake_bmw.h"
#include "gauge_core.h"
#include "diagnostic_worker.h"
#include "warm_units.h"
#include "surface_protocol.h"
#include <assert.h>
#include <poll.h>
#include <sys/wait.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
static uint64_t clock_ms=10000;
static uint64_t test_clock() { return clock_ms; }
static bool no_units() { return false; }
static unsigned launches=0;
static std::vector<std::string> launched_args;
static pid_t no_worker(const char* const* args,int) {
    ++launches;launched_args.clear();
    for (unsigned i=0;args[i];++i) launched_args.push_back(args[i]);
    return -1;
}
static surface_state captured;
#define ID6_SURFACE_SESSION_H
class SurfaceSession {
public:
    void reap() {} void closeChannel() {} bool open(const char*) { return true; }
    bool sendState(const surface_state& value,uint64_t) { captured=value;return true; }
};
#define ID6_PLUGIN_NAME "HostBoost"
#define ID6_ENTRY_PAGE 0
#define bd_mono_ms test_clock
#define launch no_worker
#define loadWarmFahrenheit no_units
#include "library.cpp"
#undef loadWarmFahrenheit
#undef launch
#undef bd_mono_ms

static void action(unsigned id) { bmw::Parcel event;assert(fake_fire(0,id,event)); }
static void poll_app(ID6LibraryLab& app) { bmw::Parcel event;assert(app.invokeDataUpdate(poll_key,event)); }
static void send_bytes(ID6LibraryLab& app,int sender,const void* bytes,size_t length) {
    const sockaddr_in peer=gauge_local_address(port(boost));
    assert(sendto(sender,bytes,length,0,reinterpret_cast<const sockaddr*>(&peer),sizeof peer)==static_cast<ssize_t>(length));
    poll_app(app);
}
static pid_t waiting_child() {
    const pid_t parent=getpid();pid_t child=fork();assert(child>=0);
    if (!child) { for (unsigned i=0;i<400 && getppid()==parent;++i) ::poll(0,0,50);_exit(0); }
    return child;
}
static std::string file_bytes(const char* path) {
    unsigned char data[64];FILE* file=fopen(path,"rb");assert(file);
    const size_t size=fread(data,1,sizeof data,file);assert(!ferror(file) && fclose(file)==0);
    return std::string(reinterpret_cast<const char*>(data),size);
}
static std::string text_model(unsigned model) {
    for (size_t i=fake_updates.size();i;--i) if (fake_updates[i-1].model==model) return fake_updates[i-1].text;
    assert(false);return "";
}
static void capture(const char* name) {
    const char* directory=getenv("FULL_BOOST_CAPTURE_DIR");if (!directory) return;
    char path[512];int size=snprintf(path,sizeof path,"%s/%s.bin",directory,name);
    assert(size>0 && static_cast<unsigned>(size)<sizeof path);
    FILE* file=fopen(path,"wb");assert(file);
    assert(fwrite(&captured,1,sizeof captured,file)==sizeof captured && fclose(file)==0);
}

int main() {
    char directory[]="/tmp/full-boost-plugin.XXXXXX";assert(mkdtemp(directory));
    char preferences[128],assignments[128],mode[128],missing[128];
    snprintf(preferences,sizeof preferences,"%s/preferences",directory);
    snprintf(assignments,sizeof assignments,"%s/assignments",directory);
    snprintf(mode,sizeof mode,"%s/mode",directory);
    snprintf(missing,sizeof missing,"%s/missing/file",directory);
    BoostLayout legacy;legacy.diesel=true;assert(legacy.save(assignments));
    const std::string original=file_bytes(assignments);
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    ID6LibraryLab app(dispatcher,bmw::BasicString<char>("HostBoost"),1,0,version,bundle,bundle,
                      missing,missing,preferences,assignments,mode);
    assert(app.onInit() && app.boostLayout_.diesel && !app.boostMode_.automatic);
    assert(file_bytes(assignments)==original && access(mode,F_OK)!=0);
    action(5580);action(5500+BD_REAGENT_DEMAND);
    action(5588);action(5500+BD_REAGENT_AVERAGE);
    const std::string custom=file_bytes(assignments);assert(custom[1]==6);
    for (unsigned i=0;i<9;++i) assert(app.boostLayout_.signal[0][i]==i);
    app.focusPage(boost,true);
    app.socket_=gauge_bind(port(boost));assert(app.socket_>=0);app.workerPage_=boost;
    pid_t child=waiting_child();app.child_=child;
    int sender=gauge_bind(BOOST_ACTIVE_PORT);assert(sender>=0);
    int wrong=socket(AF_INET,SOCK_DGRAM,0);assert(wrong>=0);
    bd_frame frame;memset(&frame,0,sizeof frame);frame.sent_ms=clock_ms;
    frame.valid=BD_BIT(BD_REAGENT_DEMAND)|BD_BIT(BD_REAGENT_AVERAGE)|BD_BIT(BD_COOLANT)|BD_BIT(BD_RPM);
    frame.value[BD_REAGENT_DEMAND]=frame.value[BD_REAGENT_AVERAGE]=0.125;
    frame.value[BD_COOLANT]=75;frame.value[BD_RPM]=1500;
    char packet[BD_AUTO_PACKET];int size=bd_encode(&frame,packet,sizeof packet);assert(size>0);
    send_bytes(app,sender,packet,size);
    assert(captured.magic==surface_magic && captured.boost_signal[0]==BD_REAGENT_DEMAND);
    assert(!strcmp(captured.values[0],"0.125 L/h") && !strcmp(captured.values[8],"0.1"));
    assert(strstr(captured.values[7],"167.0") && text_model(1840)==kBoostCaptions[BD_REAGENT_DEMAND][0]);
    capture("custom-reagent");
    action(5000+2*16+1); // Celsius updates the cached reading without a worker restart.
    assert(strstr(captured.values[7],"75.0") && app.child_==child && !app.childStopping_);
    clock_ms=10100;frame.sent_ms=clock_ms;frame.value[BD_REAGENT_DEMAND]=1;
    size=bd_encode(&frame,packet,sizeof packet);assert(size>0);
    send_bytes(app,wrong,packet,size);assert(!strcmp(captured.values[0],"0.125 L/h"));
    const char* last_space=strrchr(packet,' ');assert(last_space);
    send_bytes(app,sender,packet,static_cast<size_t>(last_space-packet));
    frame.sent_ms=clock_ms+1;size=bd_encode(&frame,packet,sizeof packet);assert(size>0);
    send_bytes(app,sender,packet,size);assert(!strcmp(captured.values[0],"0.125 L/h"));
    frame.sent_ms=9999;size=bd_encode(&frame,packet,sizeof packet);assert(size>0);
    send_bytes(app,sender,packet,size);assert(!strcmp(captured.values[0],"0.125 L/h"));
    frame.sent_ms=clock_ms;frame.value[BD_REAGENT_DEMAND]=0;
    app.boostPrefs_.choice[BOOST_COOLANT_ALERT]=1;app.boostPrefs_.choice[BOOST_CHIME]=1;
    frame.value[BD_COOLANT]=110;
    frame.age_ms[BD_COOLANT]=101; // cached temperature predates opening, despite a new packet
    size=bd_encode(&frame,packet,sizeof packet);assert(size>0);send_bytes(app,sender,packet,size);
    assert(!strcmp(captured.values[0],"0.000 L/h") && !strcmp(captured.values[7],"--"));
    frame.age_ms[BD_COOLANT]=0;
    clock_ms=11000;frame.sent_ms=clock_ms;size=bd_encode(&frame,packet,sizeof packet);assert(size>0);
    send_bytes(app,sender,packet,size);
    clock_ms=11200;frame.sent_ms=clock_ms;size=bd_encode(&frame,packet,sizeof packet);assert(size>0);
    send_bytes(app,sender,packet,size);
    assert(!strstr(captured.status,"Coolant high")); // an old sample must not prime an alert
    app.boostPrefs_.choice[BOOST_COOLANT_ALERT]=0;
    clock_ms+=BD_TEMP_STALE_MS;poll_app(app);
    assert(!strcmp(captured.values[0],"--") && !captured.boost_level[0]);
    action(5850);assert(app.automaticBoost() && file_bytes(assignments)==custom);
    assert(waitpid(child,0,0)==child);app.child_=-1;
    clock_ms+=100;app.resetBoostState();
    app.socket_=gauge_bind(port(boost));assert(app.socket_>=0);app.workerPage_=boost;
    child=waiting_child();app.child_=child;app.childStopping_=false;
    frame.sent_ms=clock_ms;memset(frame.age_ms,0,sizeof frame.age_ms);
    frame.value[BD_COOLANT]=75;
    bd_cap_frame report={app.boostToken_,clock_ms,static_cast<uint32_t>(child),
        BD_BIT(BD_RPM)|BD_BIT(BD_COOLANT)|BD_BIT(BD_OIL_TEMP),BD_BIT(BD_RPM)|BD_BIT(BD_COOLANT)};
    size=bd_auto_encode(&report,&frame,packet,sizeof packet);assert(size>0);send_bytes(app,sender,packet,size);
    assert(captured.boost_signal[0]==BD_COOLANT && captured.boost_signal[8]==BD_RPM);
    for (unsigned i=1;i<8;++i) assert(captured.boost_signal[i]==BD_EMPTY_SLOT && !strcmp(captured.values[i],"--"));
    assert(strstr(captured.values[0],"75.0") && !strcmp(captured.values[8],"1500 rpm"));
    capture("detected-defaults");
    --report.token;size=bd_auto_encode(&report,&frame,packet,sizeof packet);assert(size>0);
    send_bytes(app,sender,packet,size);assert(captured.boost_signal[0]==BD_COOLANT);
    app.boostPreferences_=missing;action(5000+8*16+1);
    assert(strstr(captured.status,"Settings not saved") && app.child_==child && !app.childStopping_);
    app.boostPreferences_=preferences;action(5000+8*16+1);assert(!strstr(captured.status,"not saved"));
    app.focusPage(boost,false);assert(waitpid(child,0,0)==child);app.child_=-1;
    assert(app.onFinalize() && captured.page==surface_hidden && app.socket_<0);
    assert(!launches);
    app.page_=boost;
    assert(!app.startWorker() && launches==1 && app.socket_<0);
    assert(launched_args.size()>=8 && launched_args[6]=="--auto-diesel" && !launched_args[7].empty());
    // The nonce is parsed as a nonzero number rather than a display-mask value.
    assert(strtoull(launched_args[7].c_str(),0,10)==app.boostToken_ && app.boostToken_);
    app.boostMode_.automatic=false;
    assert(!app.startWorker() && launches==2 && app.socket_<0);
    assert(launched_args[6]=="--signals");
    const bd_mask selected=strtoull(launched_args[7].c_str(),0,10);
    assert((selected & BD_BIT(BD_REAGENT_DEMAND)) && (selected & BD_BIT(BD_REAGENT_AVERAGE)));
    assert(app.onFinalize());
    close(sender);close(wrong);unlink(assignments);unlink(mode);
    fake_reset();
    {
        ID6LibraryLab fresh(dispatcher,bmw::BasicString<char>("FreshBoost"),1,0,version,bundle,bundle,
                            missing,missing,preferences,assignments,mode);
        assert(fresh.onInit() && fresh.boostMode_.automatic && !fresh.boostLayout_.diesel);
        fresh.focusPage(boost,true);
        bd_frame gasoline;memset(&gasoline,0,sizeof gasoline);gasoline.sent_ms=clock_ms;
        gasoline.valid=BD_LEGACY_MASK;
        const double values[]={12,14.7,4,30,70,100,3.2,90,12};
        for (unsigned i=0;i<9;++i) gasoline.value[i]=values[i];
        const int length=bd_encode(&gasoline,packet,sizeof packet);assert(length>0);
        fresh.render(packet,static_cast<unsigned>(length),clock_ms);fresh.syncSurface();capture("gasoline-live");
        action(5000+9*16+1);
        assert(fresh.automaticBoost());
        FullBoostDefaultsMode restarted;restarted.automatic=false;restarted.load(mode);assert(restarted.automatic);
        BoostLayout restarted_layout;assert(restarted_layout.load(assignments) && restarted_layout.diesel);
        assert(fresh.onFinalize());
    }
    unlink(preferences);unlink(assignments);unlink(mode);assert(rmdir(directory)==0);
    puts("Full-screen Boost production callbacks/UDP: legacy/custom persistence, 64-bit slots, units, generation/peer/time validation, zero/stale values, defaults and save errors passed.");
}
