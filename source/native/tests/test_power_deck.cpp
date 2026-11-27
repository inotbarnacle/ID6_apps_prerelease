#include "gauge_preferences.h"
#include <assert.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static const char* saved_path;
static const char production_path[]="/var/opt/hmi/data/ID5/PowerDeck.last.csv";
static unsigned launches,signals,save_calls;
static uint64_t fixture_now=100000;
static pid_t current_child;
static bool child_alive,delay_stop,fail_save;
static uint64_t fixture_mono_ms() { return fixture_now; }

static pid_t fixture_launch(const char*const* args,int) {
    assert(!child_alive);
    assert(!strcmp(args[0],"/var/opt/hmi/data/ID5/Plugins/PowerDeck/boostdeck-collector-qnx"));
    assert(!strcmp(args[3],"--session") && !strcmp(args[4],"--diesel"));
    assert(!strcmp(args[5],"--n57-sgbd") && !strcmp(args[6],"010203"));
    assert(!strcmp(args[9],"--udp-port") && !strcmp(args[10],"42872"));
    ++launches;child_alive=true;return current_child=1000+launches;
}
static pid_t fixture_waitpid(pid_t child,int*,int flags) {
    assert(child==current_child && flags==WNOHANG);
    return child_alive ? 0 : child;
}
static int fixture_kill(pid_t child,int signal) {
    assert(child==current_child && child_alive && signal==SIGTERM);
    ++signals;
    if (!delay_stop) child_alive=false;
    return 0;
}

static bool test_save(const char* path,const unsigned char* data,size_t length) {
    assert(!strcmp(path,production_path));
    ++save_calls;
    if (fail_save) return false;
    return savePersistentBytes(saved_path,data,length);
}
static FILE* test_fopen(const char* path,const char* mode) {
    return fopen(!strcmp(path,production_path) ? saved_path : path,mode);
}

/* Redirect only the CSV storage boundary; execute the production history/class. */
#define savePersistentBytes test_save
#define fopen test_fopen
#include "power_history.h"
#undef fopen
#undef savePersistentBytes

#define DIAGNOSTIC_WORKER_H
#define launch fixture_launch
#define waitpid fixture_waitpid
#define kill fixture_kill
#define bd_mono_ms fixture_mono_ms
extern "C" int n57_settings(unsigned* index) { *index=0x010203;return 1; }
#include "../src/plugins/power_deck.cpp"
#undef launch
#undef waitpid
#undef kill
#undef bd_mono_ms

static void feed(PowerDeck& app,int sender,unsigned rpm=3000,unsigned torque=500,bool coherent=true,unsigned age=0,bool demo=false) {
    fixture_now+=10;
    bd_frame frame;memset(&frame,0,sizeof frame);
    frame.sent_ms=fixture_now;
    frame.valid=BD_BIT(BD_RPM)|BD_BIT(BD_TORQUE)|BD_BIT(BD_POWER);
    frame.value[BD_RPM]=rpm;frame.value[BD_TORQUE]=torque;
    frame.value[BD_POWER]=rpm*torque*(6.283185307179586/60000);
    frame.age_ms[BD_RPM]=frame.age_ms[BD_TORQUE]=frame.age_ms[BD_POWER]=age;
    if (!coherent) frame.age_ms[BD_RPM]=age+1;
    frame.demo=demo;
    char bytes[BD_MAX_PACKET];int length=bd_encode(&frame,bytes,sizeof bytes);assert(length>0);
    bd_frame decoded;assert(n57DashboardPacket(bytes,static_cast<size_t>(length),fixture_now,0,decoded)==!demo);
    struct sockaddr_in target;memset(&target,0,sizeof target);
    target.sin_family=AF_INET;target.sin_port=htons(42872);target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(sendto(sender,bytes,length,0,reinterpret_cast<struct sockaddr*>(&target),sizeof target)==length);
    usleep(5000);
    app.testDeliver(app.testLastMessage(0x4e353744u));
}

static PowerHistory read_history() {
    PowerHistory history;assert(history.load(production_path));return history;
}
static std::string saved_bytes() {
    std::ifstream file(saved_path);
    return std::string((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
}
static void tick(PowerDeck& app,unsigned elapsed=2100) {
    fixture_now+=elapsed;app.testDeliver(app.testLastMessage(0x4e353744u));
}

int main() {
    char filename[]="/tmp/id6-power-deck-XXXXXX";int fd=mkstemp(filename);assert(fd>=0);close(fd);unlink(filename);saved_path=filename;
    int sender=socket(AF_INET,SOCK_DGRAM,0);assert(sender>=0);
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    PowerDeck app(dispatcher,bmw::BasicString<char>("PowerDeck"),1,0,version,bundle,bundle);
    assert(app.onInit());assert(app.texts[50]=="Data unavailable");
    /* A My Vehicle preview is visible without focus and must not read DDE. */
    app.testState(100,11u,23u,1);
    assert(launches==0 && !child_alive);
    app.testEvent(100,true);assert(launches==1);
    feed(app,sender);assert(app.texts[50]=="Calculated from DDE torque");
    assert(app.images[91]==60012 && app.images[92]==60011 && app.images[93]==60014); /* 214 metric PS. */
    app.testAction(160);feed(app,sender);feed(app,sender);
    assert(app.texts[50]=="REC | Recording");

    /* A short main-to-graph visibility gap must not stop the recording. */
    app.testEvent(100,false);
    unsigned stale=app.testLastMessage(0x50575356u);
    assert(stale<app.messages.size() && app.messages[stale].delay==1000);
    app.testEvent(110,true);app.testDeliver(stale);
    assert(access(saved_path,F_OK)!=0);
    feed(app,sender);assert(app.texts[50]=="REC | Recording");
    feed(app,sender,4500,600,false); /* Mixed-age triples must not enter the CSV. */

    /* An actual app exit persists all captured points after its grace event. */
    app.testEvent(110,false);app.testDeliver(app.testLastMessage(0x50575356u));
    assert(read_history().size()==3);
    app.testEvent(100,true);feed(app,sender);
    assert(app.texts[50]=="Measurement saved");
    app.testAction(160);feed(app,sender);feed(app,sender);
    app.onFinalize();assert(read_history().size()==2);

    /* Review does not collect, and stale Start actions cannot arm recording. */
    assert(app.onInit());const unsigned before=launches;
    app.testEvent(111,true);app.testAction(160);
    assert(launches==before && app.texts[50]!="REC | Recording");
    app.testEvent(111,false);app.testEvent(100,true);feed(app,sender);
    assert(app.texts[50]=="Calculated from DDE torque");
    app.onFinalize();assert(read_history().size()==2);

    /* The real HMI can keep the parent page visible underneath saved review. */
    assert(app.onInit());app.testEvent(100,true);feed(app,sender);
    const unsigned review_launches=launches;
    app.testEvent(111,true);app.testAction(160);
    fixture_now+=2100; /* Cross the normal failed-launch retry interval. */
    feed(app,sender);feed(app,sender);
    assert(launches==review_launches && app.texts[50]=="Data unavailable");
    app.testEvent(111,false);
    assert(launches==review_launches); /* Re-enable alone must not launch during navigation. */
    feed(app,sender);
    assert(launches==review_launches+1 && app.texts[50]=="Calculated from DDE torque");
    app.onFinalize();assert(read_history().size()==2);

    /* Opening saved review while recording stops/saves before loading that review. */
    assert(app.onInit());app.testEvent(100,true);app.testAction(160);feed(app,sender);
    app.testEvent(100,false);app.testEvent(111,true);
    assert(read_history().size()==1);
    app.testEvent(111,false);app.testEvent(100,true);feed(app,sender);
    assert(app.texts[50]=="Measurement saved");
    app.onFinalize();assert(read_history().size()==1);

    /* Saved RPM curves use their own models and never change the stored samples. */
    PowerHistory curve;
    for(unsigned i=0;i<=16;++i) {
        const double rpm=1000+i*250.;
        const double nm=rpm<=3500 ? 600 : 600-(rpm-3500)/15;
        PowerPoint point={i*500u,rpm,nm,nm*rpm*(6.283185307179586/60000)};
        assert(curve.push(point));
    }
    assert(curve.save(production_path));
    assert(app.onInit());const unsigned chart_launches=launches;
    app.testEvent(111,true);
    assert(launches==chart_launches);
    assert(app.images[2499]==64300); /* Fixed 400 kW, 1000 Nm and corresponding metric PS. */
    assert(app.texts[2510]=="600 Nm" && app.texts[2520]=="262 kW / 356 PS");
    assert(app.texts[2500]=="Peaks: 600 Nm @ 3500 RPM | 262 kW / 356 PS @ 5000 RPM");
    assert(app.properties[std::make_pair(4190u,3u)]==1u);
    assert(app.properties[std::make_pair(4191u,3u)]==1u);
    assert(app.properties[std::make_pair(4190u,20u)]>=PowerRpmPlot::Left);
    assert(app.properties[std::make_pair(4191u,20u)]+280u<=
           PowerRpmPlot::Left+PowerRpmPlot::Segments*PowerRpmPlot::Step);
    assert(app.images[2404]>=63000 && app.images[2404]<63601);
    assert(app.images[2428]>=63601 && app.images[2428]<64202);
    assert(app.properties[std::make_pair(4004u,21u)]==150u);
    assert(read_history().size()==17 && read_history().at(16).rpm==5000);
    app.testEvent(111,false);app.testEvent(110,true);
    assert(app.texts[2300]=="Waiting for live samples");
    app.onFinalize();assert(read_history().size()==17);
    curve.clear();
    for(unsigned i=0;i<=10;++i) {
        const double rpm=1000+i*500.;
        PowerPoint point={i*500u,rpm,1000.,1000.*rpm*(6.283185307179586/60000)};
        assert(curve.push(point));
    }
    assert(curve.save(production_path));
    assert(app.onInit());app.testEvent(111,true);
    assert(app.texts[2500].find("off scale")!=std::string::npos);
    assert(app.texts[2510]=="1000 Nm" && app.properties[std::make_pair(4190u,3u)]==1u);
    assert(app.texts[2520].empty() && app.properties[std::make_pair(4191u,3u)]==0u);
    assert(app.images[2447]==64202); // No false power plateau at the plot ceiling.
    app.onFinalize();assert(read_history().at(10).kw>600);

    /* Native OPTIONS sends focus0 while the same PowerDeck page stays visible.
     * Keep collecting and recording for arbitrarily long option selection. */
    assert(app.onInit());app.testEvent(100,true);feed(app,sender);
    app.testAction(160);feed(app,sender);
    assert(app.testState(100,1u,4u,0));feed(app,sender);
    fixture_now+=2100;feed(app,sender);
    assert(app.texts[50]=="REC | Recording");
    assert(read_history().at(10).kw>600); // Options did not prematurely replace the saved run.
    app.testState(100,1u,4u,1);feed(app,sender);
    app.testState(100,11u,23u,0); // MENU's actual visibility exit ends the recording.
    app.testDeliver(app.testLastMessage(0x50575356u));
    assert(read_history().size()==4);
    const unsigned hidden_launches=launches;
    fixture_now+=2100;feed(app,sender);feed(app,sender);
    assert(launches==hidden_launches && !child_alive && app.texts[50]=="Measurement saved");
    app.testAction(160);tick(app);assert(launches==hidden_launches && !child_alive);
    app.testState(100,1u,4u,1);feed(app,sender);assert(launches==hidden_launches);
    app.onFinalize();

    /* Losing focus in saved review (including its OPTIONS) never resumes polling. */
    assert(app.onInit());app.testEvent(100,true);feed(app,sender);
    app.testEvent(111,true);const unsigned saved_launches=launches;
    app.testState(111,1u,4u,0);fixture_now+=2100;feed(app,sender);
    app.testState(111,1u,4u,1);feed(app,sender);
    assert(launches==saved_launches && read_history().size()==4);
    app.onFinalize();

    /* Preview does not poll unless the owner explicitly starts a recording.
     * Start from native OPTIONS can arm it while focus is absent; Stop immediately
     * restores the ordinary preview rule and preserves the finished CSV. */
    assert(app.onInit());app.testEvent(100,true);feed(app,sender);
    const unsigned active_launches=launches,active_signals=signals;
    app.testState(100,1u,4u,0);assert(signals==active_signals+1 && !child_alive);
    tick(app);tick(app);assert(launches==active_launches);
    app.testState(100,11u,23u,1);tick(app);assert(launches==active_launches);
    app.testAction(160);assert(child_alive && launches==active_launches+1);
    feed(app,sender);assert(app.texts[50]=="REC | Recording");
    tick(app,10000);feed(app,sender);assert(child_alive && signals==active_signals+1);
    app.testAction(161);assert(!child_alive && app.texts[50]=="Measurement saved");
    const std::string finished=saved_bytes();assert(read_history().size()==2);
    tick(app);tick(app);assert(!child_alive && saved_bytes()==finished);
    app.onFinalize();

    /* Empty and rejected triples have a useful status, never overwrite the
     * previous measurement, and never reach the filesystem save boundary. */
    assert(app.onInit());app.testEvent(100,true);feed(app,sender);
    const unsigned prior_saves=save_calls;
    app.testAction(161);assert(app.texts[50]=="No data to save");
    assert(save_calls==prior_saves && saved_bytes()==finished);
    feed(app,sender);assert(app.texts[50]=="No data to save");
    app.testAction(160);assert(app.texts[50]=="REC | Recording");
    feed(app,sender,3000,500,false); // A visible RPM alone is not a coherent triple.
    feed(app,sender,3000,500,true,BD_STALE_MS);
    feed(app,sender,3000,500,true,0,true);
    app.testAction(161);assert(app.texts[50]=="No data to save");
    assert(save_calls==prior_saves && saved_bytes()==finished);

    /* A real storage-boundary failure is distinct from an empty measurement.
     * A new Start clears that error and does not replace the old CSV early. */
    app.testAction(160);feed(app,sender,3500,550);
    fail_save=true;app.testAction(161);fail_save=false;
    assert(app.texts[50]=="Could not save measurement" && saved_bytes()==finished);
    feed(app,sender);assert(app.texts[50]=="Could not save measurement");
    app.testAction(160);assert(app.texts[50]=="REC | Recording" && saved_bytes()==finished);
    feed(app,sender,4000,600);app.testAction(161);
    assert(app.texts[50]=="Measurement saved" && read_history().size()==1 && read_history().at(0).rpm==4000);
    app.onFinalize();

    /* Saved ignores Start, including while its main parent remains visible.
     * Returning to live and explicitly starting a new run records only that run. */
    assert(app.onInit());app.testEvent(100,true);app.testAction(160);feed(app,sender);
    app.testEvent(111,true);assert(!child_alive);
    const unsigned blocked_launches=launches;
    const std::string reviewed=saved_bytes();
    app.testAction(160);tick(app);assert(launches==blocked_launches && !child_alive);
    app.testState(111,1u,4u,0);tick(app);app.testState(111,1u,4u,1);
    assert(launches==blocked_launches && saved_bytes()==reviewed);
    app.testEvent(111,false);app.testState(100,1u,4u,1);tick(app);
    app.testAction(160);assert(saved_bytes()==reviewed);
    feed(app,sender,4500,650);app.testAction(161);
    assert(read_history().size()==1 && read_history().at(0).rpm==4500);
    app.onFinalize();

    /* Missing/malformed Map activity cannot be treated as a recording preview. */
    assert(app.onInit());app.testEvent(100,true);app.testAction(160);feed(app,sender);
    bmw::Map<unsigned,bmw::Variant> empty;
    app.testMap(100,1u,empty);assert(!child_alive);
    fixture_now+=1000;app.testDeliver(app.testLastMessage(0x50575356u));
    const unsigned invalid_launches=launches;
    tick(app);app.testAction(160);tick(app);assert(launches==invalid_launches);
    app.testState(100,1u,4u,2);tick(app);assert(launches==invalid_launches);
    app.testState(100,1u,23u,1);tick(app);assert(launches==invalid_launches);
    app.testState(100,1u,4u,1);tick(app);assert(launches==invalid_launches+1);
    app.testEvent(111,true);app.testMap(111,11u,empty);app.testAction(160);
    tick(app);assert(!child_alive && launches==invalid_launches+1);
    app.testState(111,11u,23u,0);tick(app);assert(child_alive);
    app.onFinalize();

    /* A slow stop followed by rapid refocus cannot create a second worker. */
    assert(app.onInit());app.testEvent(100,true);delay_stop=true;
    const unsigned rapid_launches=launches;
    app.testState(100,1u,4u,0);assert(child_alive);
    app.testState(100,1u,4u,1);tick(app);assert(launches==rapid_launches);
    child_alive=false;tick(app);assert(launches==rapid_launches+1);
    delay_stop=false;
    const unsigned queued=app.testLastMessage(0x4e353744u);
    app.onFinalize();app.testDeliver(queued);assert(!child_alive);
    assert(app.onInit());app.testDeliver(queued);assert(launches==rapid_launches+1);
    app.testState(100,11u,23u,1);tick(app);assert(launches==rapid_launches+1);
    app.testState(100,1u,4u,1);tick(app);assert(launches==rapid_launches+2);
    app.onFinalize();assert(!child_alive);
    close(sender);unlink(saved_path);
    puts("PowerDeck actual class: preview/REC/OPTIONS/MENU PID lifecycle, typed maps, stale callbacks, coherent recording, save errors and Saved transitions OK");
}
