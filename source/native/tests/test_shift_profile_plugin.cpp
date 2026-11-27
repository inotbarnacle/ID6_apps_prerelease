#include "fake_bmw.h"
#include <assert.h>
#include <string.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
static uint64_t test_now=10000;
extern "C" uint64_t bd_mono_ms(void) { return test_now; }

// Exercise production callbacks/drawing; the framework is a host fake only.
#include "../src/plugins/shift_deck.cpp"

static FakeUpdate last(unsigned model) {
    for (std::vector<FakeUpdate>::reverse_iterator it=fake_updates.rbegin();it!=fake_updates.rend();++it)
        if (it->model==model) return *it;
    assert(!"model was never updated");
    return FakeUpdate();
}
static void action(unsigned id) {
    bmw::Parcel event;
    assert(fake_fire(0,id,event));
}
static void sample(ShiftDeck& deck, int rpm) {
    deck.frame_.sampled_ms=test_now;
    deck.frame_.rpm=rpm;
    deck.frame_.kph=100;
    deck.rpm_started_=0;
    deck.draw(test_now);
}
static void snapshot(const char* directory, const char* name) {
    if (!directory) return;
    char path[256];
    int n=snprintf(path,sizeof path,"%s/%s.models",directory,name);
    assert(n>0 && static_cast<unsigned>(n)<sizeof path);
    FILE* file=fopen(path,"wb");
    assert(file);
    for (unsigned i=0;i<fake_updates.size();++i) {
        const FakeUpdate& update=fake_updates[i];
        if (update.kind=='i') fprintf(file,"%u %u\n",update.model,update.number);
    }
    assert(fclose(file)==0);
}

int main(int argc, char** argv) {
    const char* previews=argc==2 ? argv[1] : 0;
    char path[]="/tmp/shift-profile-plugin.XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    const unsigned char legacy[]={'G',1,2,1};
    assert(savePersistentBytes(path,legacy,sizeof legacy));
    bmw::IDispatcher dispatcher;
    bmw::Bundle bundle;
    bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    {
        ShiftDeck deck(dispatcher,bmw::BasicString<char>("ShiftDeck"),1,0,version,bundle,bundle,path);
        assert(deck.onInit());
        assert(deck.child_==-1); // no diagnostic worker is needed by these tests
        assert(last(56).number==kTargetBase+2 && last(66).number==55918);
        assert(last(64).number==56660 && last(53).text=="Shift light at 6250 RPM");
        bmw::Parcel show(true);
        assert(fake_fire(100,11,show));
        sample(deck,6249);
        assert(last(57).number==kShiftOff && last(55).number==kLightBase+11);
        sample(deck,6250);
        assert(last(57).number==kShiftOn);
        snapshot(previews,"gasoline");

        action(49); // diesel profile, retained 4000 RPM target, 6000 display
        assert(last(66).number==kDieselScaleBase+2 && last(56).number==kDieselTargetBase+30);
        sample(deck,3999);
        assert(last(57).number==kShiftOff);
        sample(deck,4000);
        assert(last(57).number==kShiftOn);
        action(50); // 3900 target
        assert(last(53).text=="Shift light at 3900 RPM");
        action(51);
        assert(deck.prefs_.diesel_target==4000);
        action(52); // smaller scale clamps the target to 3900
        assert(deck.prefs_.diesel_target==3900 && last(66).number==kDieselScaleBase);
        action(54);
        assert(last(66).number==kDieselScaleBase+2);
        action(53); // 5000 display
        assert(last(66).number==kDieselScaleBase+1);
        assert(last(68).number==kDieselMarkBase+64+29);
        sample(deck,2500);
        assert(last(54).number==kSweepBase+64);
        snapshot(previews,"diesel");

        ShiftPreferences stored;
        assert(stored.load(path) && stored.diesel && stored.diesel_target==3900);
        assert(stored.gasoline_target==2 && stored.metric && stored.diesel_scale==1);
        action(48); // gasoline restored, not a shared diesel target
        assert(last(53).text=="Shift light at 6250 RPM" && last(66).number==55918);
        assert(last(56).number==kTargetBase+2 && last(68).number==kMarkBase+2);
        action(49);
        sample(deck,4500);
        assert(last(57).number==kShiftOn);
        test_now+=DRIVE_STALE_MS;
        deck.draw(test_now);
        assert(last(57).number==kShiftOff && last(55).number==kLightBase);
        assert(last(54).number==kSweepBase && last(61).number==kDigitBase+10);
        snapshot(previews,"stale");
        // Old samples, invalid RPM and future samples cannot leave the light on.
        sample(deck,-1);
        assert(last(57).number==kShiftOff);
        deck.frame_.rpm=4500;
        deck.frame_.sampled_ms=test_now+1;
        deck.draw(test_now);
        assert(last(57).number==kShiftOff);
        sample(deck,4500);
        bmw::Parcel hide(false);
        assert(fake_fire(100,11,hide));
        assert(last(57).number==kShiftOff);

        action(41); // explicit gasoline preset returns to gasoline profile
        assert(!deck.prefs_.diesel && deck.prefs_.targetRpm()==5800);
        action(46);
        assert(last(64).number==55916 && !deck.prefs_.metric);
        assert(deck.onFinalize());
    }
    fake_reset();
    {
        ShiftDeck deck(dispatcher,bmw::BasicString<char>("ShiftDeck"),1,0,version,bundle,bundle,
                       "/nonexistent-idrive-directory/preferences");
        assert(deck.onInit());
        action(49);
        assert(last(81).text.find("not saved")!=std::string::npos);
        assert(deck.onFinalize());
    }
    unlink(path);
    puts("ShiftDeck production actions, gasoline/diesel rendering, stale/hidden light suppression and save failure passed.");
}
