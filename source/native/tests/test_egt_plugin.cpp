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
    char directory[]="/tmp/egt-plugin.XXXXXX"; assert(mkdtemp(directory));
    char prefs[256],assignments[256],mode[256];
    snprintf(prefs,sizeof prefs,"%s/prefs",directory); snprintf(assignments,sizeof assignments,"%s/signals",directory);
    snprintf(mode,sizeof mode,"%s/signals.defaults",directory);
    const unsigned char metric[]={'G',1,0,1}; assert(savePersistentBytes(prefs,metric,sizeof metric));
    bmw::IDispatcher dispatcher; bmw::Bundle bundle; bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    {
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(app.onInit() && app.fd_>=0);
        action(503); action(507); // explicit Diesel / Custom settings
        bmw::Parcel show(true); assert(fake_fire(510,11,show));
        action(600+BD_EGT_B2S4);
        assert(app.signals_[1][0]==BD_EGT_B2S4 && launched.empty());
        app.gauges_visible_=true;
        app.startVisibleCollector();
        assert(app.collector_<0 && !launched.empty());
        bd_mask requested=0;
        for (unsigned i=0;i+1<launched.size();++i)
            if (launched[i]=="--signals") requested=strtoull(launched[i+1].c_str(),0,10);
        assert(requested & BD_BIT(BD_EGT_B2S4));
        assert(requested>UINT32_MAX && !(requested & ~BD_ALL_MASK));
        bd_frame frame; memset(&frame,0,sizeof frame);
        frame.sent_ms=bd_mono_ms(); frame.valid=BD_BIT(BD_EGT_B2S4); frame.value[BD_EGT_B2S4]=600.5;
        send_packet(app,frame);
        assert(last(370).text=="EGT B2 S4" && last(300).text=="600.5 \260C");
        assert(last(320).number>kBarFirst);
        action(2440); assert(last(300).text=="1112.9 \260F");
        action(2441); assert(last(300).text=="600.5 \260C");
        frame.sent_ms=bd_mono_ms(); frame.age_ms[BD_EGT_B2S4]=BD_TEMP_STALE_MS;
        send_packet(app,frame); assert(last(300).text=="--");
        frame.valid=0; frame.sent_ms=bd_mono_ms(); send_packet(app,frame);
        assert(last(300).text=="--" && last(370).text=="EGT B2 S4");
        assert(app.onFinalize());
    }
    fake_reset();
    {
        BoostDeck restored(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(restored.onInit() && restored.diesel_ && !restored.defaults_mode_.automatic);
        assert(restored.signals_[1][0]==BD_EGT_B2S4 && restored.onFinalize());
    }
    unsigned char stored[21];
    FILE* file=fopen(assignments,"rb"); assert(file);
    assert(fread(stored,1,sizeof stored,file)==sizeof stored && fgetc(file)==EOF);
    assert(fclose(file)==0 && stored[0]=='B' && stored[1]==3);
    stored[1]=2; // the same reserved channel in an old-format file must stay invalid
    assert(savePersistentBytes(assignments,stored,sizeof stored));
    fake_reset();
    {
        BoostDeck invalid(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(invalid.onInit() && !invalid.diesel_ && invalid.signals_[1][0]==BD_BOOST);
        assert(invalid.onFinalize());
    }
    unlink(prefs); unlink(assignments); unlink(mode); rmdir(directory);
    puts("Production EGT selection, 64-bit collector argv, UDP display, units, freshness and saved assignments passed.");
}
