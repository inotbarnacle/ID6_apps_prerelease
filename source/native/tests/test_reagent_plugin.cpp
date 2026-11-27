#include "fake_bmw.h"
#include "diagnostic_worker.h"
#include "gauge_coexistence.h"
#include <assert.h>
namespace bmw { class IDispatcher {}; class Bundle {}; }
static std::vector<std::string> launched;
static pid_t capture_launch(const char *const *args,int output=-1) {
    (void)output;launched.clear();
    for (unsigned i=0;args[i];++i) launched.push_back(args[i]);
    return -1; // no diagnostic worker or tone is ever executed
}
#define launch capture_launch
#include "../src/plugins/boost_deck.cpp"
#undef launch
static FakeUpdate last(unsigned model) {
    for (std::vector<FakeUpdate>::reverse_iterator i=fake_updates.rbegin();i!=fake_updates.rend();++i)
        if (i->model==model) return *i;
    assert(!"model not updated");return FakeUpdate();
}
static void action(unsigned id) { bmw::Parcel event;assert(fake_fire(0,id,event)); }
static void edit(unsigned state) { bmw::Parcel show(true);assert(fake_fire(state,11,show)); }
static void capture_models() {
    const char *path=getenv("REAGENT_MODEL_CAPTURE");if (!path) return;
    FILE *file=fopen(path,"wb");assert(file);
    for (unsigned slot=0;slot<9;++slot) {
        fprintf(file,"%u\ts\t%s\n",300+slot,last(300+slot).text.c_str());
        fprintf(file,"%u\ts\t%s\n",370+slot,last(370+slot).text.c_str());
        fprintf(file,"%u\ti\t%u\n",320+slot,last(320+slot).number);
        fprintf(file,"%u\ti\t%u\n",330+slot,last(330+slot).number);
    }
    assert(!ferror(file) && fclose(file)==0);
}
static void send_packet(BoostDeck &app,bd_frame &frame) {
    frame.sent_ms=bd_mono_ms();
    char packet[BD_MAX_PACKET];const int size=bd_encode(&frame,packet,sizeof packet);assert(size>0);
    int source=socket(AF_INET,SOCK_DGRAM,0);assert(source>=0);
    const sockaddr_in destination=gauge_local_address(BD_UDP_PORT);
    assert(sendto(source,packet,size,0,reinterpret_cast<const sockaddr*>(&destination),sizeof destination)==size);
    close(source);app.poll();
}
int main() {
    char directory[]="/tmp/reagent-plugin.XXXXXX";assert(mkdtemp(directory));
    char prefs[256],assignments[256],mode[256];
    snprintf(prefs,sizeof prefs,"%s/prefs",directory);snprintf(assignments,sizeof assignments,"%s/signals",directory);
    snprintf(mode,sizeof mode,"%s/signals.defaults",directory);
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    {
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(app.onInit() && app.fd_>=0);action(503);action(507);
        edit(510);action(600+BD_REAGENT_LEVEL);
        app.gauges_visible_=true;
        bd_frame frame;memset(&frame,0,sizeof frame);
        frame.valid=BD_BIT(BD_REAGENT_LEVEL)|BD_BIT(BD_REAGENT_AVERAGE)|BD_BIT(BD_REAGENT_DEMAND);
        frame.value[BD_REAGENT_LEVEL]=100;frame.value[BD_REAGENT_AVERAGE]=.005;frame.value[BD_REAGENT_DEMAND]=1.25;
        send_packet(app,frame);assert(last(370).text=="Reagent level" && last(300).text=="100.0%");
        frame.value[BD_REAGENT_LEVEL]=0;send_packet(app,frame);assert(last(300).text=="0.0%");
        frame.age_ms[BD_REAGENT_LEVEL]=BD_TEMP_STALE_MS;send_packet(app,frame);assert(last(300).text=="--");
        frame.age_ms[BD_REAGENT_LEVEL]=0;action(600+BD_REAGENT_AVERAGE);send_packet(app,frame);
        assert(last(370).text=="Avg reagent use" && last(300).text=="0.005 L/h");
        action(2402);assert(last(300).text=="0.005 L/h"); // pressure unit selection never changes reagent units
        frame.value[BD_REAGENT_AVERAGE]=327.675;send_packet(app,frame);
        assert(last(300).text=="327.675 L/h"); // numeric value survives the 10 L/h visual scale
        action(600+BD_REAGENT_DEMAND);send_packet(app,frame);
        assert(last(370).text=="Avg reagent req." && last(300).text=="1.250 L/h");
        edit(518);action(600+BD_REAGENT_AVERAGE);send_packet(app,frame);
        assert(last(378).text=="Use L/h" && last(308).text=="327.7");
        edit(511);action(600+BD_REAGENT_LEVEL);
        edit(512);action(600+BD_REAGENT_AVERAGE);
        frame.value[BD_REAGENT_LEVEL]=50.196078;send_packet(app,frame);capture_models();
        app.gauges_visible_=true;app.startVisibleCollector();
        bd_mask requested=0;
        for (unsigned i=0;i+1<launched.size();++i)
            if (launched[i]=="--signals") requested=strtoull(launched[i+1].c_str(),0,10);
        assert(requested & BD_BIT(BD_REAGENT_AVERAGE));
        frame.valid&=~BD_BIT(BD_REAGENT_AVERAGE);send_packet(app,frame);
        assert(last(308).text=="--" && last(300).text=="1.250 L/h");
        assert(app.onFinalize());
    }
    unsigned char stored[21];FILE *file=fopen(assignments,"rb");assert(file);
    assert(fread(stored,1,sizeof stored,file)==sizeof stored && fgetc(file)==EOF && !ferror(file));
    assert(fclose(file)==0 && stored[0]=='B' && stored[1]==6);
    fake_reset();
    {
        BoostDeck restored(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(restored.onInit() && restored.diesel_ && !restored.defaults_mode_.automatic);
        assert(restored.signals_[1][0]==BD_REAGENT_DEMAND && restored.signals_[1][8]==BD_REAGENT_AVERAGE);
        assert(restored.onFinalize());
    }
    for (unsigned revision=2;revision<=6;++revision) {
        unsigned char invalid[21];memcpy(invalid,stored,sizeof invalid);invalid[1]=(unsigned char)revision;
        invalid[3+9]=revision==6 ? BD_COUNT : BD_REAGENT_LEVEL;
        assert(savePersistentBytes(assignments,invalid,sizeof invalid));fake_reset();
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(app.onInit() && !app.diesel_ && app.onFinalize());
    }
    // Each frozen revision remains accepted at its own last valid ID.
    for (unsigned revision=2;revision<=5;++revision) {
        unsigned char old[21];memcpy(old,stored,sizeof old);old[1]=(unsigned char)revision;
        const unsigned last_id=revision==5 ? 54 : revision==4 ? 46 : revision==3 ? 38 : 30;
        for (unsigned i=3;i<sizeof old;++i) old[i]=(unsigned char)last_id;
        assert(savePersistentBytes(assignments,old,sizeof old));fake_reset();
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,prefs,assignments);
        assert(app.onInit() && app.diesel_ && app.signals_[1][0]==last_id);
        assert(app.saveAssignments() && app.onFinalize());
        file=fopen(assignments,"rb");assert(file);unsigned char saved[21];
        assert(fread(saved,1,sizeof saved,file)==sizeof saved && fclose(file)==0 && saved[1]==revision);
    }
    unlink(prefs);unlink(assignments);unlink(mode);rmdir(directory);
    puts("Production reagent callbacks/UDP, units/precision, zero/stale, narrow caption and B2-B6 migration passed.");
}
