#include "shift_preferences.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

static void same(const char* path, const unsigned char* expected, size_t length) {
    unsigned char bytes[16];
    FILE* file=fopen(path,"rb");
    assert(file && fread(bytes,1,sizeof bytes,file)==length && !ferror(file));
    fclose(file);
    assert(!memcmp(bytes,expected,length));
}

int main() {
    char directory[]="/tmp/shift-preferences.XXXXXX";
    assert(mkdtemp(directory));
    char gasoline[128], legacy[128], profile[128];
    snprintf(gasoline,sizeof gasoline,"%s/v2",directory);
    snprintf(legacy,sizeof legacy,"%s/legacy",directory);
    snprintf(profile,sizeof profile,"%s/profile",directory);
    ShiftPreferences prefs;
    assert(prefs.valid() && prefs.targetRpm()==6500 && prefs.scaleRpm()==8000);

    // Every current upstream gasoline target and unit choice keeps G1 semantics.
    for (unsigned target=0;target<21;++target) for (unsigned metric=0;metric<2;++metric) {
        const unsigned char current[]={'G',1,static_cast<unsigned char>(target),static_cast<unsigned char>(metric)};
        assert(savePersistentBytes(gasoline,current,sizeof current));
        assert(prefs.load(gasoline,legacy,profile));
        assert(!prefs.diesel && prefs.gasoline_target==target && prefs.metric==(metric!=0));
        assert(prefs.targetRpm()==6000+target*100 && prefs.scaleRpm()==8000);
        same(gasoline,current,sizeof current);
        assert(access(profile,F_OK)!=0);
        for (int rpm=-1;rpm<=10000;++rpm) {
            unsigned bar=rpm>0 ? (rpm*128+4000)/8000 : 0;
            if (bar>128) bar=128;
            const int approach=rpm-(static_cast<int>(prefs.targetRpm())-2000);
            const unsigned lights=rpm>=static_cast<int>(prefs.targetRpm()) ? 13u :
                approach>0 ? static_cast<unsigned>(approach)*12/2000 : 0u;
            assert(prefs.sweep(rpm)==bar && prefs.lights(rpm)==lights);
        }
        assert(prefs.saveGasoline(gasoline));
        same(gasoline,current,sizeof current);
    }
    unlink(gasoline);

    // The legacy file uses five indices, unlike the current v2 G1 file.
    const unsigned converted[]={0,0,3,5,7};
    for (unsigned target=0;target<5;++target) for (unsigned metric=0;metric<2;++metric) {
        const unsigned char old[]={'G',1,static_cast<unsigned char>(target),static_cast<unsigned char>(metric)};
        assert(savePersistentBytes(legacy,old,sizeof old));
        assert(prefs.load(gasoline,legacy,profile));
        assert(!prefs.diesel && prefs.gasoline_target==converted[target] && prefs.metric==(metric!=0));
        assert(prefs.targetRpm()==6000+converted[target]*100);
        same(legacy,old,sizeof old);
        assert(access(gasoline,F_OK)!=0 && access(profile,F_OK)!=0); // no writes on load
    }
    for (unsigned target=0;target<5;++target) for (unsigned metric=0;metric<2;++metric)
        for (unsigned diesel=0;diesel<2;++diesel) for (unsigned scale=0;scale<3;++scale) {
            const unsigned char old[]={'S',1,static_cast<unsigned char>(diesel),static_cast<unsigned char>(target),
                static_cast<unsigned char>(metric),20,static_cast<unsigned char>(scale),0};
            assert(savePersistentBytes(legacy,old,sizeof old));
            assert(prefs.load(gasoline,legacy,profile));
            assert(prefs.gasoline_target==converted[target] && prefs.metric==(metric!=0));
            assert(prefs.diesel==(diesel!=0) && prefs.diesel_target==3000 && prefs.diesel_scale==scale);
            same(legacy,old,sizeof old);
            assert(access(gasoline,F_OK)!=0 && access(profile,F_OK)!=0);
        }

    // A current gasoline file wins over legacy gas fields; S2 supplies only profile choices.
    const unsigned char current[]={'G',1,20,1};
    const unsigned char old[]={'S',1,1,2,0,20,1,0};
    assert(savePersistentBytes(gasoline,current,sizeof current));
    assert(savePersistentBytes(legacy,old,sizeof old));
    prefs.gasoline_target=20; prefs.metric=true; prefs.diesel=true;
    for (unsigned scale=0;scale<3;++scale) {
        prefs.setDieselScale(scale);
        const unsigned maximum=prefs.scaleRpm();
        assert(prefs.sweep(maximum/2)==64 && prefs.sweep(maximum)==128);
        assert(prefs.sweep(INT_MAX)==128 && prefs.sweep(INT_MIN)==0);
        unsigned last=0;
        for (unsigned rpm=0;rpm<maximum+2000;++rpm) {
            unsigned next=prefs.sweep(rpm);
            assert(next>=last && next<=128); last=next;
        }
        for (unsigned target=1000;target<maximum;target+=100) {
            prefs.diesel_target=target;
            assert(prefs.valid() && prefs.lights(-1)==0 && prefs.lights(target-1000)==0);
            assert(prefs.lights(target-500)==6 && prefs.lights(target-1)==11);
            assert(prefs.lights(target)==13 && prefs.lights(INT_MAX)==13);
            assert(prefs.saveProfile(profile));
            ShiftPreferences restarted;
            assert(restarted.load(gasoline,legacy,profile));
            assert(restarted.diesel && restarted.targetRpm()==target && restarted.scaleRpm()==maximum);
            assert(restarted.gasoline_target==20 && restarted.metric);
            same(gasoline,current,sizeof current); same(legacy,old,sizeof old);
            const unsigned char encoded[]={'S',2,1,static_cast<unsigned char>((target-1000)/100),static_cast<unsigned char>(scale)};
            same(profile,encoded,sizeof encoded);
        }
    }
    prefs.setDieselScale(0);
    assert(prefs.diesel_target==3900);
    for (unsigned i=0;i<100;++i) prefs.adjustDiesel(false);
    assert(prefs.diesel_target==1000);
    for (unsigned i=0;i<100;++i) prefs.adjustDiesel(true);
    assert(prefs.diesel_target==3900);
    prefs.setDieselScale(99); assert(prefs.diesel_scale==0);

    // A corrupt or truncated new sidecar cannot reactivate the old S1 diesel mode.
    const unsigned char bad[][6]={
        {'S',1,1,30,2,0}, {'S',3,1,30,2,0}, {'S',2,2,30,2,0},
        {'S',2,1,50,2,0}, {'S',2,1,30,3,0}, {'S',2,1,30,0,0}
    };
    for (unsigned i=0;i<sizeof bad/sizeof bad[0];++i) {
        assert(savePersistentBytes(profile,bad[i],5));
        ShiftPreferences restarted;
        assert(restarted.load(gasoline,legacy,profile));
        assert(!restarted.diesel && restarted.gasoline_target==20 && restarted.metric);
        assert(restarted.diesel_target==4000 && restarted.diesel_scale==2);
    }
    const unsigned char good[]={'S',2,1,30,2,0};
    for (unsigned length=0;length<=6;++length) {
        if (length==5) continue;
        assert(savePersistentBytes(profile,good,length));
        ShiftPreferences restarted;
        assert(restarted.load(gasoline,legacy,profile) && !restarted.diesel);
    }
    unlink(profile); unlink(gasoline);
    const unsigned char bad_legacy[][9]={
        {'G',1,5,0,0,0,0,0,0}, {'G',1,1,2,0,0,0,0,0},
        {'S',1,2,3,0,30,2,0,0}, {'S',1,1,5,0,30,2,0,0},
        {'S',1,1,3,2,30,2,0,0}, {'S',1,1,3,0,50,2,0,0},
        {'S',1,1,3,0,30,3,0,0}, {'S',1,1,3,0,30,2,1,0}
    };
    for (unsigned i=0;i<sizeof bad_legacy/sizeof bad_legacy[0];++i) {
        assert(savePersistentBytes(legacy,bad_legacy[i],i<2 ? 4 : 8));
        ShiftPreferences rejected;
        assert(!rejected.load(gasoline,legacy,profile) && rejected.targetRpm()==6500);
    }
    unlink(legacy);
    const unsigned char bad_current[]={'G',1,21,0};
    assert(savePersistentBytes(gasoline,bad_current,sizeof bad_current));
    ShiftPreferences rejected;
    assert(!rejected.load(gasoline,legacy,profile) && rejected.targetRpm()==6500);
    assert(!prefs.saveProfile("/nonexistent-idrive-directory/preferences"));
    assert(!prefs.saveGasoline("/nonexistent-idrive-directory/preferences"));
    prefs.diesel_target=1234;
    assert(!prefs.valid() && !prefs.saveProfile(profile));
    unlink(gasoline); rmdir(directory);
    puts("ShiftDeck: 21 gasoline targets, G1/S1 migration, separate S2 profile, scales, corrupt files and source preservation passed.");
}
