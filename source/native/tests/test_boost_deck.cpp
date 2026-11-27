#include "gauge_preferences.h"
#include "gauge_core.h"
#include "n57.h"
#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

static char fixture_dir[128];
static bool fail_assignments,fail_preferences,fail_names,fail_rail_scale;
static unsigned configured_n57=0x010203;
static uint64_t fixture_now=100000;
static int fixture_udp_fd=-1;
static std::string storagePath(const char* path) {
    const char* prefix="/var/opt/hmi/data/ID5/";
    return strncmp(path,prefix,strlen(prefix)) ? path : std::string(fixture_dir)+"/"+(path+strlen(prefix));
}
static FILE* fixture_fopen(const char* path,const char* mode) {
    return fopen(storagePath(path).c_str(),mode);
}
static bool fixture_save(const char* path,const unsigned char* bytes,size_t length) {
    if ((fail_assignments && strstr(path,"BoostDeck.signals")) ||
        (fail_preferences && strstr(path,"BoostDeck.prefs")) ||
        (fail_rail_scale && strstr(path,"BoostDeck.rail-scale")) ||
        (fail_names && strstr(path,"BoostDeck.panels"))) return false;
    return savePersistentBytes(storagePath(path).c_str(),bytes,length);
}
#define fopen fixture_fopen
#define savePersistentBytes fixture_save
#include "boost_preferences.h"
#include "boost_rail_scale.h"
#include "boostdeck_assignments.h"
#undef fopen
#undef savePersistentBytes

struct Child {
    bool exited,stopping,tone;
    Child(bool is_tone):exited(false),stopping(false),tone(is_tone){}
};
static std::map<pid_t,Child> children;
static std::vector<std::vector<std::string> > launches;
static pid_t next_pid=100;
static bool tone_echild;
static int tone_probe_error;
static unsigned tone_probes,tone_terminations;
static unsigned collectorLaunches() {
    unsigned n=0;
    for (unsigned i=0;i<launches.size();++i) if (launches[i].size()>1 && launches[i][1]!="--alert-tone") ++n;
    return n;
}
static unsigned activeCollectors() {
    unsigned n=0;
    for (std::map<pid_t,Child>::const_iterator i=children.begin();i!=children.end();++i)
        if (!i->second.tone) ++n;
    return n;
}
static pid_t fixture_launch(const char* const* args,int) {
    std::vector<std::string> copied;
    for (unsigned i=0;args[i];++i) copied.push_back(args[i]);
    const bool tone=copied.size()>1 && copied[1]=="--alert-tone";
    assert(tone || activeCollectors()==0);
    launches.push_back(copied);
    children.insert(std::make_pair(++next_pid,Child(tone)));
    return next_pid;
}
static pid_t fixture_waitpid(pid_t pid,int*,int flags) {
    assert(flags==WNOHANG);
    std::map<pid_t,Child>::iterator child=children.find(pid);
    if (child==children.end()) {errno=ECHILD;return -1;}
    if (child->second.tone && tone_echild) {errno=ECHILD;return -1;}
    if (!child->second.exited) return 0;
    children.erase(child);return pid;
}
static int fixture_kill(pid_t pid,int signal) {
    std::map<pid_t,Child>::iterator child=children.find(pid);
    if (!signal) {
        if (child!=children.end() && child->second.tone) ++tone_probes;
        if (child==children.end() || child->second.exited) {
            if (child!=children.end()) children.erase(child);
            errno=ESRCH;return -1;
        }
        if (child->second.tone && tone_probe_error) {errno=tone_probe_error;return -1;}
        return 0;
    }
    assert(signal==SIGTERM && child!=children.end());
    child->second.stopping=true;
    if (child->second.tone) ++tone_terminations;
    return 0;
}
static unsigned fixture_n57(const char*) {return configured_n57;}
static uint64_t fixture_clock() {return fixture_now;}
static int fixture_bind(int fd,const struct sockaddr* address,socklen_t length) {
    const int result=bind(fd,address,length);
    if (!result && address->sa_family==AF_INET &&
        ntohs(reinterpret_cast<const struct sockaddr_in*>(address)->sin_port)==BD_UDP_PORT) fixture_udp_fd=fd;
    return result;
}
#define DIAGNOSTIC_WORKER_H
#define launch fixture_launch
#define waitpid fixture_waitpid
#define kill fixture_kill
#define n57_index_file fixture_n57
#define bd_mono_ms fixture_clock
#define bind fixture_bind
#include "../src/plugins/boost_deck.cpp"
#undef launch
#undef waitpid
#undef kill
#undef n57_index_file
#undef bd_mono_ms
#undef bind

static void tick(BoostDeck& app) {app.testDeliver(app.testLastMessage(kPollKey));}
static void finishStopped(BoostDeck& app) {
    for (std::map<pid_t,Child>::iterator i=children.begin();i!=children.end();++i)
        if (i->second.stopping) i->second.exited=true;
    tick(app);
}
static bd_mask launchedMask() {
    for (unsigned i=static_cast<unsigned>(launches.size());i>0;--i) {
        const std::vector<std::string>& args=launches[i-1];
        for (unsigned j=0;j+1<args.size();++j)
            if (args[j]=="--signals") return strtoull(args[j+1].c_str(),0,10);
    }
    assert(false);return 0;
}

static std::string storageBytes(const char* path) {
    FILE* file=fopen(storagePath(path).c_str(),"rb");assert(file);
    char data[128];const size_t length=fread(data,1,sizeof data,file);
    assert(!ferror(file) && feof(file));fclose(file);
    return std::string(data,length);
}

static void sampleValue(BoostDeck& app,unsigned signal,double value,unsigned age=0,bool valid=true) {
    bd_frame frame;memset(&frame,0,sizeof frame);
    frame.sent_ms=++fixture_now;
    frame.valid=valid ? BD_BIT(signal) : 0;
    frame.value[signal]=value;frame.age_ms[signal]=age;
    char packet[BD_MAX_PACKET];const int length=bd_encode(&frame,packet,sizeof packet);assert(length>0);
    const int sender=socket(AF_INET,SOCK_DGRAM,0);assert(sender>=0);
    struct sockaddr_in destination;memset(&destination,0,sizeof destination);
    destination.sin_family=AF_INET;destination.sin_port=htons(BD_UDP_PORT);
    destination.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(sendto(sender,packet,length,0,reinterpret_cast<struct sockaddr*>(&destination),sizeof destination)==length);
    close(sender);
    // Receive before the next fake-clock jump can make a delayed packet stale.
    struct pollfd ready={fixture_udp_fd,POLLIN,0};
    assert(fixture_udp_fd>=0 && poll(&ready,1,100)==1 && (ready.revents&POLLIN));
    tick(app);
}

static void sampleRail(BoostDeck& app,double pressure,unsigned age=0,bool valid=true) {
    sampleValue(app,BD_RAIL,pressure,age,valid);
}

static pid_t activeTone() {
    pid_t result=-1;
    for (std::map<pid_t,Child>::const_iterator i=children.begin();i!=children.end();++i)
        if (i->second.tone) {assert(result<0);result=i->first;}
    return result;
}

static pid_t tonePidChecks(BoostDeck& app) {
    tone_echild=true;
    app.testAction(2480); // Screen + soft chime.
    sampleValue(app,BD_COOLANT,120);
    fixture_now+=1300;sampleValue(app,BD_COOLANT,120);
    const pid_t first=activeTone();assert(first>0);
    for (unsigned i=0;i<4;++i) {fixture_now+=100;tick(app);}
    assert(tone_probes>=4 && activeTone()==first && !children.find(first)->second.stopping);
    assert(tone_terminations==0);

    // The auto-reaped tone really exits: ESRCH releases it without a stop signal.
    children.find(first)->second.exited=true;
    tick(app);assert(activeTone()<0 && tone_terminations==0);
    sampleValue(app,BD_COOLANT,90); // Recover the alert before triggering another chime.
    fixture_now+=12000;sampleValue(app,BD_COOLANT,120);
    fixture_now+=1300;sampleValue(app,BD_COOLANT,120);
    const pid_t second=activeTone();assert(second>first);
    tone_probe_error=EPERM;
    const unsigned before=tone_probes;
    for (unsigned i=0;i<4;++i) {fixture_now+=100;tick(app);}
    assert(tone_probes>=before+4 && activeTone()==second && !children.find(second)->second.stopping);
    assert(tone_terminations==0);
    return second; // MENU below must still own and stop this live PID.
}

static void railScaleChecks(BoostDeck& app) {
    assert(app.texts[128]=="Diesel rail scale: 0 - 2000 bar");
    assert(app.texts[375]=="Rail 0-2000");
    sampleRail(app,1000);
    assert(app.texts[305]=="1000 bar" && app.images[335]==57008);
    const unsigned launched=collectorLaunches();
    const std::string saved_assignments=storageBytes(kAssignments),saved_prefs=storageBytes(kPreferences);
    static const unsigned dials[]={12,8,6,5};
    for (unsigned i=0;i<BoostRailScale::COUNT;++i) {
        app.testAction(2560+i);
        assert(app.bools[5160+i]);
        assert(app.images[335]==57002+dials[i] && app.texts[305]=="1000 bar");
        assert(storageBytes(kPreferences)==saved_prefs && storageBytes(kAssignments)==saved_assignments);
        assert(collectorLaunches()==launched);
    }
    app.testAction(2562);
    app.testAction(2460);
    assert(app.images[325]==55016 && app.images[335]==kTransparent);
    app.testAction(2400);
    assert(app.texts[305]=="14504 psi" && app.images[325]==55016 && app.texts[375]=="Rail 0-29008");
    app.testAction(2402);
    assert(app.texts[305]=="100.0 MPa" && app.images[325]==55016);
    assert(app.texts[128]=="Diesel rail scale: 0 - 200 MPa");
    assert(app.texts[375]=="Rail 0-200");
    app.testAction(2401);
    fail_rail_scale=true;app.testAction(2560);fail_rail_scale=false;
    assert(app.texts[118]=="Could not save diesel rail scale" && app.bools[5162]);
    assert(app.images[325]==55016 && collectorLaunches()==launched);

    assert(app.testState(518,11,23,1));app.testAction(600+BD_RAIL);finishStopped(app);
    assert(app.testState(518,11,23,0));
    sampleRail(app,1000);
    assert(app.texts[378]=="Rail\n0-2000" && app.images[328]==56110);
    assert(app.texts[308]=="1000" && app.texts[380]=="bar" && app.texts[305]=="1000 bar");
    app.testAction(2400);
    assert(app.texts[308]=="14504" && app.texts[380]=="psi" && app.texts[305]=="14504 psi");
    app.testAction(2402);
    assert(app.texts[378]=="Rail\n0-200" && app.texts[308]=="100.0" && app.texts[380]=="MPa");
    assert(app.texts[305]=="100.0 MPa");
    app.testAction(2401);
    app.testAction(2461);
    assert(app.images[338]==57008 && app.images[335]==57008);
    sampleRail(app,3000);
    assert(app.texts[305]=="3000 bar" && app.images[335]==57014);
    sampleRail(app,1000,BD_STALE_MS);
    assert(app.texts[305]=="--" && app.texts[308]=="--" && app.texts[380].empty() && app.images[338]==kDialFirst);
    sampleRail(app,1000,0,false);
    assert(app.texts[305]=="--" && app.texts[308]=="--" && app.texts[380].empty());

    app.testAction(502);finishStopped(app);app.testAction(2460);sampleRail(app,125);
    assert(app.texts[375]=="Rail press." && app.images[325]==55016 && app.texts[305]=="125 bar");
    app.testAction(2560);
    assert(app.images[325]==55016 && app.texts[305]=="125 bar");
    app.testAction(503);finishStopped(app);app.testAction(2562);
    assert(app.texts[360]=="Engine profile: N57");
    app.testAction(2461);
}

static void assignSignal(BoostDeck& app,unsigned slot,unsigned signal) {
    assert(app.testState(510u+slot,11,23,1));
    app.testAction(600u+signal);finishStopped(app);
    assert(app.testState(510u+slot,11,23,0));
}

static void splitUnitChecks(BoostDeck& app) {
    unsigned char saved[2][kBoostDeckSlots];
    bool diesel,hidden;
    loadBoostDeckAssignments(kAssignments,saved,diesel,hidden,true);
    assert(diesel);
    for (unsigned slot=0;slot<kBoostDeckSlots;++slot) assignSignal(app,slot,BD_MAP);
    assert(app.texts[308]=="--" && app.texts[380].empty());
    app.testAction(2402);sampleValue(app,BD_MAP,230);
    for (unsigned slot=0;slot<8;++slot) assert(app.texts[300u+slot]=="230.0 kPa");
    assert(app.texts[308]=="230.0" && app.texts[380]=="kPa");

    // The number stays unchanged: units must update independently of the number cache.
    sampleValue(app,BD_MAP,0);
    assert(app.texts[308]=="0.0" && app.texts[380]=="kPa");
    app.testAction(2401);assert(app.texts[308]=="0.0" && app.texts[380]=="bar");
    app.testAction(2400);assert(app.texts[308]=="0.0" && app.texts[380]=="psi");
    app.testAction(2402);assert(app.texts[308]=="0.0" && app.texts[380]=="kPa");
    sampleValue(app,BD_MAP,230,BD_TEMP_STALE_MS);
    assert(app.texts[308]=="--" && app.texts[380].empty());
    sampleValue(app,BD_MAP,230,0,false);
    assert(app.texts[308]=="--" && app.texts[380].empty());

    assignSignal(app,8,BD_REL_TORQUE_DDE);sampleValue(app,BD_REL_TORQUE_DDE,75);
    assert(app.texts[308]=="75.0" && app.texts[380]=="%");
    assignSignal(app,8,BD_COOLANT);app.testAction(2441);sampleValue(app,BD_COOLANT,90);
    assert(app.texts[308]=="90.0" && app.texts[380]=="\260C");
    app.testAction(2440);

    app.testAction(502);finishStopped(app);assignSignal(app,8,BD_AFR);sampleValue(app,BD_AFR,14.7);
    assert(app.texts[308]=="14.7" && app.texts[380].empty());
    assignSignal(app,8,saved[0][8]);app.testAction(503);finishStopped(app);
    for (unsigned slot=0;slot<kBoostDeckSlots;++slot) assignSignal(app,slot,saved[1][slot]);
    app.testAction(2401);
}

int main() {
    char directory[]="/tmp/id6-boost-deck-XXXXXX";assert(mkdtemp(directory));
    strcpy(fixture_dir,directory);
    unsigned char initial[2][kBoostDeckSlots];
    bool diesel,hidden;
    loadBoostDeckAssignments("/nonexistent",initial,diesel,hidden,true);
    initial[1][0]=BD_REL_TORQUE_DDE;initial[1][1]=BD_ADBLUE;
    assert(saveBoostDeckAssignments(kAssignments,initial,true,false));
    BoostPreferences prefs;
    prefs.choice[BOOST_STYLE]=1;prefs.choice[BOOST_PRESSURE]=1;
    prefs.choice[BOOST_COOLANT_ALERT]=1;prefs.choice[BOOST_OIL_PRESSURE_ALERT]=1;
    assert(prefs.save(kPreferences));
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle);
    assert(app.onInit());
    assert(app.texts[360]=="Engine profile: N57");
    assert(app.texts[123]=="Gauge style: Circular dials");
    assert(app.testState(100,11,23,1));tick(app);assert(collectorLaunches()==0);
    assert(app.testState(100,1,4,1));tick(app);
    assert(collectorLaunches()==1 && (launchedMask()&BD_BIT(BD_REL_TORQUE_DDE)));
    assert(launchedMask()&BD_BIT(BD_ADBLUE));
    assert(launchedMask()&BD_BIT(BD_COOLANT));
    assert(!(launchedMask()&BD_BIT(BD_OIL))); // N57 has no pressure sensor.
    assert(launches.back().size()>=9 && launches.back().back()=="010203");

    railScaleChecks(app);
    splitUnitChecks(app);

    app.testAction(504);finishStopped(app);
    assert(!(launchedMask()&BD_BIT(BD_ADBLUE)));
    assert(app.properties[std::make_pair(261u,3u)]==0);
    loadBoostDeckAssignments(kAssignments,initial,diesel,hidden,true);
    assert(diesel && hidden && initial[1][1]==BD_ADBLUE);
    app.testAction(505);finishStopped(app);
    assert(launchedMask()&BD_BIT(BD_ADBLUE));

    assert(app.testState(510,11,23,1));
    app.testAction(600+BD_TORQUE);finishStopped(app);
    assert((launchedMask()&BD_BIT(BD_TORQUE)) && !(launchedMask()&BD_BIT(BD_REL_TORQUE_DDE)));
    assert(app.testState(510,11,23,0));

    fail_assignments=true;
    assert(app.testState(510,11,23,1));app.testAction(600+BD_REL_TORQUE_DDE);tick(app);
    assert(!(launchedMask()&BD_BIT(BD_REL_TORQUE_DDE)));
    fail_assignments=false;
    const pid_t tone=tonePidChecks(app);
    assert(app.testState(100,1,4,0));
    assert(children.find(tone)->second.stopping && tone_terminations>0);
    finishStopped(app);
    assert(activeTone()<0);
    tone_echild=false;tone_probe_error=0;
    const unsigned stopped=collectorLaunches();
    fixture_now+=10000;tick(app);assert(collectorLaunches()==stopped);
    assert(activeCollectors()==0);
    app.onFinalize();
    app.texts[380]="old unit"; // A retained HMI model must be cleared even when the internal unit cache is empty.
    assert(app.onInit());
    assert(app.texts[308]=="--" && app.texts[380].empty());
    assert(app.texts[128]=="Diesel rail scale: 0 - 2000 bar" && app.bools[5162]);
    assert(app.texts[378]=="Rail\n0-2000");
    app.onFinalize();
    unlink(storagePath(kAssignments).c_str());unlink(storagePath(kPreferences).c_str());
    unlink(storagePath(kRailScalePreferences).c_str());
    assert(rmdir(directory)==0);
    puts("BoostDeck upstream single panel: N57 selectors, masks, dials/settings, emissions and focus stop OK");
}
