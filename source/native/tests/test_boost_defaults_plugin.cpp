#include "fake_bmw.h"
#include <assert.h>
#include <sys/wait.h>
#include <poll.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
#include "../src/plugins/boost_deck.cpp"

static FakeUpdate last(unsigned model) {
    for (std::vector<FakeUpdate>::reverse_iterator i=fake_updates.rbegin();i!=fake_updates.rend();++i)
        if (i->model==model) return *i;
    assert(!"model not updated"); return FakeUpdate();
}
static void action(unsigned id) { bmw::Parcel event; assert(fake_fire(0,id,event)); }
static std::string bytes(const char* path) {
    FILE* file=fopen(path,"rb"); assert(file);
    char data[128]; size_t size=fread(data,1,sizeof data,file);
    assert(!ferror(file) && feof(file)); fclose(file);
    return std::string(data,size);
}
static void legacy(const char* path, unsigned version) {
    unsigned char data[25]={'B',static_cast<unsigned char>(version),1};
    unsigned stride=version==1 ? 11 : 9;
    for (unsigned profile=0;profile<2;++profile)
        for (unsigned i=0;i<stride;++i) data[3+profile*stride+i]=static_cast<unsigned char>((i+profile)%BD_COUNT);
    assert(savePersistentBytes(path,data,3+2*stride));
}
static void send_packet(BoostDeck& app, const char* packet, int size) {
    int sock=socket(AF_INET,SOCK_DGRAM,0); assert(sock>=0 && size>0);
    sockaddr_in dest; memset(&dest,0,sizeof dest);
    dest.sin_family=AF_INET; dest.sin_addr.s_addr=htonl(INADDR_LOOPBACK); dest.sin_port=htons(BD_UDP_PORT);
    assert(sendto(sock,packet,size,0,reinterpret_cast<sockaddr*>(&dest),sizeof dest)==size);
    close(sock); app.poll();
}

int main() {
    char directory[]="/tmp/boost-defaults-plugin.XXXXXX";
    assert(mkdtemp(directory));
    char prefs[256],assignments[256],mode[256];
    snprintf(prefs,sizeof prefs,"%s/prefs",directory);
    snprintf(assignments,sizeof assignments,"%s/signals",directory);
    snprintf(mode,sizeof mode,"%s/signals.defaults",directory);
    const unsigned char g1[]={'G',1,0,1};
    assert(savePersistentBytes(prefs,g1,sizeof g1));
    bmw::IDispatcher dispatcher; bmw::Bundle bundle; bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    {
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(app.onInit() && app.fd_>=0 && app.defaults_mode_.automatic && !app.diesel_);
        action(503);
        assert(app.automaticDiesel() && !bd_slots_mask(app.automatic_slots_));
        assert(last(300).text=="--" && last(370).text.empty());
        const std::string saved=bytes(assignments);
        // The stand-in child never executes diagnostics: it only waits for SIGTERM.
        pid_t child=fork(); assert(child>=0);
        if (!child) {
            const pid_t parent=getppid();
            for (unsigned i=0;i<200 && getppid()==parent;++i) poll(0,0,50);
            _exit(0);
        }
        app.collector_=child; app.capability_token_=42; app.gauges_visible_=true;
        bd_cap_frame report={42,bd_mono_ms(),static_cast<uint32_t>(child),
                             (1u<<BD_COOLANT)|(1u<<BD_RAIL)|(1u<<BD_OIL_TEMP),
                             (1u<<BD_COOLANT)|(1u<<BD_RAIL)};
        bd_frame sample; memset(&sample,0,sizeof sample);
        sample.sent_ms=report.sent_ms; sample.valid=report.known;
        sample.value[BD_COOLANT]=59; sample.value[BD_RAIL]=1000; sample.value[BD_OIL_TEMP]=85;
        char packet[BD_AUTO_PACKET], ordinary[BD_MAX_PACKET];
        int size=bd_auto_encode(&report,&sample,packet,sizeof packet);
        assert(size>0); send_packet(app,packet,size);
        assert(app.displaySignal(0)==BD_COOLANT && app.displaySignal(1)==BD_RAIL);
        assert(app.displaySignal(2)==BD_EMPTY_SLOT); // a valid-looking unadvertised oil value cannot select a gauge
        assert(last(300).text=="59.0 \260C" && last(301).text=="1000 bar" && last(302).text=="--");
        assert(bytes(assignments)==saved);
        bd_cap_frame regression=report; regression.known=regression.supported=0;
        size=bd_auto_encode(&regression,&sample,packet,sizeof packet);
        assert(!app.acceptPacket(packet,size));
        int ordinary_size=bd_encode(&sample,ordinary,sizeof ordinary);
        assert(!app.acceptPacket(ordinary,ordinary_size));
        report.token=41;
        size=bd_auto_encode(&report,&sample,packet,sizeof packet); assert(!app.acceptPacket(packet,size));
        report.token=42; ++report.worker_pid;
        size=bd_auto_encode(&report,&sample,packet,sizeof packet); assert(!app.acceptPacket(packet,size));
        --report.worker_pid;
        report.sent_ms=sample.sent_ms=app.received_-1;
        size=bd_auto_encode(&report,&sample,packet,sizeof packet); assert(!app.acceptPacket(packet,size));
        report.sent_ms=sample.sent_ms=bd_mono_ms()+5000;
        size=bd_auto_encode(&report,&sample,packet,sizeof packet); assert(!app.acceptPacket(packet,size));
        report.sent_ms=sample.sent_ms=bd_mono_ms()-BD_STALE_MS;
        size=bd_auto_encode(&report,&sample,packet,sizeof packet); assert(!app.acceptPacket(packet,size));
        report.sent_ms=sample.sent_ms=bd_mono_ms();
        sample.age_ms[BD_RAIL]=BD_STALE_MS;
        size=bd_auto_encode(&report,&sample,packet,sizeof packet); send_packet(app,packet,size);
        assert(last(301).text=="--" && app.displaySignal(1)==BD_RAIL);
        assert(last(300).text=="59.0 \260C");
        report.known|=BD_BIT(BD_EGT_B1S3)|BD_BIT(BD_EGT_B2S4);
        report.supported|=BD_BIT(BD_EGT_B1S3)|BD_BIT(BD_EGT_B2S4);
        sample.valid|=BD_BIT(BD_EGT_B1S3)|BD_BIT(BD_EGT_B2S4);
        sample.value[BD_EGT_B1S3]=600.5; sample.value[BD_EGT_B2S4]=450;
        report.sent_ms=sample.sent_ms=bd_mono_ms();
        size=bd_auto_encode(&report,&sample,packet,sizeof packet); send_packet(app,packet,size);
        assert(app.displaySignal(2)==BD_EGT_B1S3 && app.displaySignal(3)==BD_EMPTY_SLOT);
        assert(last(302).text=="600.5 \260C" && last(372).text=="EGT B1 S3");
        bd_cap_frame narrower=report;
        narrower.known &= ~BD_BIT(BD_EGT_B2S4); narrower.supported &= ~BD_BIT(BD_EGT_B2S4);
        size=bd_auto_encode(&narrower,&sample,packet,sizeof packet);
        assert(!app.acceptPacket(packet,size)); // high-bit evidence must not be truncated in the UI cache
        sample.valid &= ~BD_BIT(BD_EGT_B1S3);
        size=bd_auto_encode(&report,&sample,packet,sizeof packet); send_packet(app,packet,size);
        assert(app.displaySignal(2)==BD_EGT_B1S3 && last(302).text=="--");
        assert(bytes(assignments)==saved);
        app.gauges_visible_=false;
        assert(!app.acceptPacket(packet,size));
        app.gauges_visible_=true; app.stopping_=true;
        assert(!app.acceptPacket(packet,size)); app.stopping_=false;
        app.assignmentsChanged(); // clears cached support and stops the stand-in
        assert(waitpid(child,0,0)==child);
        app.collector_=-1; app.stopping_=false; app.gauges_visible_=false;
        assert(last(370).text.empty() && last(300).text=="--");
        assert(!app.acceptPacket(packet,size));
        action(507);
        assert(!app.defaults_mode_.automatic && last(361).text=="Diesel gauges: Custom layout");
        assert(bytes(assignments)==saved); // mode changes never rewrite user selections
        app.gauges_visible_=true;
        sample.sent_ms=bd_mono_ms();
        ordinary_size=bd_encode(&sample,ordinary,sizeof ordinary);
        assert(app.acceptPacket(ordinary,ordinary_size));
        app.gauges_visible_=false;
        assert(app.onFinalize());
    }
    for (unsigned revision=1;revision<=2;++revision) {
        legacy(assignments,revision); unlink(mode); fake_reset();
        const std::string before=bytes(assignments);
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(app.onInit() && !app.defaults_mode_.automatic);
        for (unsigned i=0;i<9;++i) assert(app.displaySignal(i)==i+1);
        assert(bytes(assignments)==before);
        action(506);
        assert(app.defaults_mode_.automatic && bytes(assignments)==before);
        action(507);
        assert(!app.defaults_mode_.automatic && bytes(assignments)==before);
        app.defaults_path_[0]=0;
        action(506);
        assert(!app.defaults_mode_.automatic && last(361).text=="Could not save diesel gauge mode");
        assert(app.onFinalize());
    }
    // Strict sidecar parsing and restart, independent of B1/B2 and G1/G2 formats.
    BoostDefaultsMode saved;
    for (unsigned choice=0;choice<2;++choice) {
        saved.automatic=choice!=0; assert(saved.save(mode));
        BoostDefaultsMode loaded; loaded.automatic=!saved.automatic; loaded.load(mode);
        assert(loaded.automatic==saved.automatic);
    }
    const unsigned char invalid[][4]={{'D',2,0,0},{'D',1,2,0},{'D',1,0,1},{'X',1,0,0}};
    for (unsigned i=0;i<4;++i) {
        assert(savePersistentBytes(mode,invalid[i],i==2 ? 4 : 3));
        saved.automatic=true; saved.load(mode); assert(saved.automatic);
    }
    for (unsigned n=0;n<3;++n) {
        assert(savePersistentBytes(mode,invalid[0],n));
        saved.automatic=true; saved.load(mode); assert(saved.automatic);
    }
    unlink(mode); unlink(assignments); unlink(prefs); assert(rmdir(directory)==0);
    puts("BoostDeck automatic/custom callbacks, production UDP, identity/freshness guards, legacy layouts and save errors passed.");
}
