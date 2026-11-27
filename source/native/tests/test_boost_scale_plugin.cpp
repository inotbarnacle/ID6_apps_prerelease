#include "fake_bmw.h"
#include <assert.h>
#include <sys/stat.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
// Production callbacks and rendering; never show the dashboard or launch a worker.
#include "../src/plugins/boost_deck.cpp"

static FakeUpdate last(unsigned model) {
    for (std::vector<FakeUpdate>::reverse_iterator it=fake_updates.rbegin();it!=fake_updates.rend();++it)
        if (it->model==model) return *it;
    assert(!"model not updated");
    return FakeUpdate();
}
static void action(unsigned id) {
    bmw::Parcel event;
    assert(fake_fire(0,id,event));
}
static void sample(BoostDeck& app, double pressure) {
    memset(&app.frame_,0,sizeof app.frame_);
    app.received_=bd_mono_ms();
    app.frame_.valid=1u<<BD_RAIL;
    app.frame_.value[BD_RAIL]=pressure;
    app.refresh();
    assert(app.collector_<=0 && app.tone_<=0);
}
static std::string bytes(const char* path) {
    FILE* file=fopen(path,"rb");
    assert(file);
    char data[128];
    const size_t size=fread(data,1,sizeof data,file);
    assert(!ferror(file) && feof(file));
    fclose(file);
    return std::string(data,size);
}

int main() {
    char directory[]="/tmp/boost-scale-plugin.XXXXXX";
    assert(mkdtemp(directory));
    char prefs[256],assignments[256],scale[256];
    snprintf(prefs,sizeof prefs,"%s/prefs",directory);
    snprintf(assignments,sizeof assignments,"%s/signals",directory);
    snprintf(scale,sizeof scale,"%s/scale",directory);
    const unsigned char legacy[]={'G',1,0,1};
    assert(savePersistentBytes(prefs,legacy,sizeof legacy));
    bmw::IDispatcher dispatcher;
    bmw::Bundle bundle;
    bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    {
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,
                      prefs,assignments,scale);
        assert(app.onInit() && app.fd_>=0);
        assert(app.rail_scale_.maximum()==2000 && !app.diesel_);
        assert(last(128).text=="Diesel rail scale: 0 - 2000 bar");
        action(503);
        assert(last(375).text=="Rail 0-2000");
        const std::string saved_assignments=bytes(assignments);
        sample(app,1000);
        assert(last(305).text=="1000 bar" && last(325).number==55016);
        assert(last(307).text=="--");
        static const unsigned fills[]={32,20,16,13};
        for (unsigned i=0;i<BoostRailScale::COUNT;++i) {
            action(2560+i);
            assert(app.rail_scale_.choice==i);
            assert(last(325).number==55000+fills[i] && last(305).text=="1000 bar");
            assert(bytes(prefs)==std::string(reinterpret_cast<const char*>(legacy),sizeof legacy));
            assert(bytes(assignments)==saved_assignments);
        }
        action(2562);
        action(2461); // circular style
        assert(last(335).number==57008 && last(325).number==kTransparent);
        action(2400); // psi changes the labels, not the fill
        assert(last(305).text=="14504 psi" && last(335).number==57008);
        assert(last(375).text=="Rail 0-29008");
        action(2402);
        assert(last(305).text=="100000 kPa" && last(335).number==57008);
        assert(last(128).text=="Diesel rail scale: 0 - 200000 kPa");
        action(2401);
        // Assign rail to the ninth tile using the existing configuration callback.
        bmw::Parcel edit(true);
        assert(fake_fire(518,11,edit));
        action(600+BD_RAIL);
        assert(last(378).text=="Rail\n0-2000");
        sample(app,1000);
        assert(last(338).number==57008);
        action(2460);
        assert(last(328).number==56110 && last(325).number==55016);
        sample(app,3000); // numeric reading is never clipped by the display scale
        assert(last(305).text=="3000 bar" && last(325).number==55032);
        app.frame_.age_ms[BD_RAIL]=BD_STALE_MS;
        app.refresh();
        assert(last(305).text=="--" && last(325).number==kBarFirst && last(328).number==kVerticalFirst);
        action(502); // original gasoline caption and 250-bar fill
        sample(app,125);
        assert(last(375).text=="Rail press." && last(325).number==55016);
        action(2560);
        assert(last(325).number==55016 && last(305).text=="125 bar");
        assert(app.onFinalize());
    }
    fake_reset();
    {
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,
                      prefs,assignments,scale);
        assert(app.onInit() && app.rail_scale_.choice==0 && !app.diesel_);
        action(503);
        assert(last(378).text=="Rail\n0-1000");
        assert(app.onFinalize());
    }
    fake_reset();
    {
        BoostDeck app(dispatcher,bmw::BasicString<char>("BoostDeck"),1,0,version,bundle,bundle,
                      prefs,assignments,"/nonexistent-idrive-directory/scale");
        assert(app.onInit());
        action(2560);
        assert(last(118).text=="Could not save diesel rail scale");
        assert(app.rail_scale_.choice==BoostRailScale::DEFAULT);
        assert(app.onFinalize());
    }
    unlink(scale); unlink(assignments); unlink(prefs);
    assert(rmdir(directory)==0);
    puts("BoostDeck production range callbacks, bars/dials/vertical, units, staleness and save failure passed.");
}
