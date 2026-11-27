#include "fake_bmw.h"
#include "diagnostic_worker.h"
#include <assert.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
static std::vector<std::string> launched;
static pid_t capture_launch(const char* const* arguments,int output=-1) {
    (void)output; launched.clear();
    for (unsigned i=0;arguments[i];++i) launched.push_back(arguments[i]);
    return -1; // never execute a diagnostic worker or a tone
}
#define launch capture_launch
#include "../src/plugins/boost_deck.cpp"
#undef launch

static FakeUpdate last(unsigned model) {
    for (std::vector<FakeUpdate>::reverse_iterator i=fake_updates.rbegin();i!=fake_updates.rend();++i)
        if (i->model==model) return *i;
    assert(!"model not updated"); return FakeUpdate();
}
static void action(unsigned id) { bmw::Parcel event; assert(fake_fire(0,id,event)); }
static void send_packet(BoostDeck& app,const bd_frame& frame) {
    char packet[BD_MAX_PACKET]; int n=bd_encode(&frame,packet,sizeof packet); assert(n>0);
    int source=socket(AF_INET,SOCK_DGRAM,0); assert(source>=0);
    sockaddr_in destination; memset(&destination,0,sizeof destination);
    destination.sin_family=AF_INET; destination.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    destination.sin_port=htons(BD_UDP_PORT);
    assert(sendto(source,packet,n,0,reinterpret_cast<const sockaddr*>(&destination),sizeof destination)==n);
    close(source); app.poll();
}

int main() {
    char directory[]="/tmp/pressure-plugin.XXXXXX"; assert(mkdtemp(directory));
    char prefs[256],assignments[256],mode[256];
    snprintf(prefs,sizeof prefs,"%s/prefs",directory); snprintf(assignments,sizeof assignments,"%s/signals",directory);
    snprintf(mode,sizeof mode,"%s/signals.defaults",directory);
    bmw::IDispatcher dispatcher; bmw::Bundle bundle; bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    {
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(app.onInit() && app.fd_>=0);
        action(503); action(507);
        bmw::Parcel show(true); assert(fake_fire(510,11,show));
        for (unsigned signal=BD_RAIL_A_REQUEST;signal<=BD_BOOST_B_ACTUAL;++signal) {
            action(600+signal);
            assert(app.signals_[1][0]==signal);
        }
        app.gauges_visible_=true; app.startVisibleCollector();
        bd_mask requested=0;
        for (unsigned i=0;i+1<launched.size();++i)
            if (launched[i]=="--signals") requested=strtoull(launched[i+1].c_str(),0,10);
        assert(requested & BD_BIT(BD_BOOST_B_ACTUAL));
        bd_frame frame; memset(&frame,0,sizeof frame);
        frame.sent_ms=bd_mono_ms(); frame.valid=BD_BIT(BD_BOOST_B_ACTUAL); frame.value[BD_BOOST_B_ACTUAL]=200;
        action(2402); send_packet(app,frame);
        assert(last(370).text=="Boost B act. abs" && last(300).text=="200.0 kPa");
        action(2401); assert(last(300).text=="2.0 bar");
        action(2400); assert(last(300).text=="29.0 psi");
        frame.sent_ms=bd_mono_ms(); frame.value[BD_BOOST_B_ACTUAL]=0; send_packet(app,frame);
        assert(last(300).text=="0.0 psi");
        frame.sent_ms=bd_mono_ms(); frame.value[BD_BOOST_B_ACTUAL]=2047.96875; send_packet(app,frame);
        assert(last(300).text=="297.0 psi"); // numeric survives visual scale clamp
        frame.sent_ms=bd_mono_ms(); frame.age_ms[BD_BOOST_B_ACTUAL]=BD_TEMP_STALE_MS; send_packet(app,frame);
        assert(last(300).text=="--");
        action(600+BD_RAIL_B_ACTUAL); action(2401);
        frame.sent_ms=bd_mono_ms(); frame.valid=BD_BIT(BD_RAIL_B_ACTUAL); frame.value[BD_RAIL_B_ACTUAL]=3000;
        send_packet(app,frame); assert(last(300).text=="3000.0 bar" && last(370).text=="Rail B act.");
        action(600+BD_BOOST_B_ACTUAL);
        assert(app.onFinalize());
    }
    fake_reset();
    {
        BoostDeck restored(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(restored.onInit() && restored.diesel_ && !restored.defaults_mode_.automatic);
        assert(restored.signals_[1][0]==BD_BOOST_B_ACTUAL && restored.onFinalize());
    }
    unsigned char stored[21];
    FILE* file=fopen(assignments,"rb"); assert(file);
    assert(fread(stored,1,sizeof stored,file)==sizeof stored && fgetc(file)==EOF);
    assert(fclose(file)==0 && stored[0]=='B' && stored[1]==5);
    for (unsigned revision=2;revision<=4;++revision) {
        stored[1]=(unsigned char)revision;
        assert(savePersistentBytes(assignments,stored,sizeof stored));
        fake_reset();
        BoostDeck invalid(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(invalid.onInit() && !invalid.diesel_ && invalid.signals_[1][0]==BD_BOOST && invalid.onFinalize());
    }
    stored[1]=5; stored[3+9]=55;
    assert(savePersistentBytes(assignments,stored,sizeof stored));
    fake_reset();
    {
        BoostDeck invalid(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(invalid.onInit() && !invalid.diesel_ && invalid.onFinalize());
    }
    unlink(prefs); unlink(assignments); unlink(mode); rmdir(directory);
    puts("Production pressure callbacks/UDP, reference labels, units, range clamp, freshness and B5 migration bounds passed.");
}
