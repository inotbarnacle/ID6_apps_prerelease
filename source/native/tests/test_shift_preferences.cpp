#include "shift_preferences.h"
#include <assert.h>
#include <limits.h>
#include <string.h>

int main() {
    char path[]="/tmp/shift-preferences.XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    ShiftPreferences prefs;
    assert(prefs.valid() && prefs.targetRpm()==6500 && prefs.scaleRpm()==8000);
    const unsigned targets[]={5800,6000,6250,6500,6700};
    for (unsigned target=0;target<5;++target) for (unsigned metric=0;metric<2;++metric) {
        const unsigned char legacy[]={'G',1,static_cast<unsigned char>(target),static_cast<unsigned char>(metric)};
        assert(savePersistentBytes(path,legacy,sizeof legacy));
        assert(prefs.load(path));
        assert(!prefs.diesel && prefs.gasoline_target==target && prefs.metric==(metric!=0));
        assert(prefs.targetRpm()==targets[target] && prefs.scaleRpm()==8000);
        // The upgrade reads, but does not rewrite, the old file.
        FILE* file=fopen(path,"rb");
        unsigned char bytes[4];
        assert(file && fread(bytes,1,sizeof bytes,file)==4 && fgetc(file)==EOF);
        fclose(file);
        assert(!memcmp(bytes,legacy,sizeof legacy));
        for (int rpm=-1;rpm<=10000;++rpm) {
            unsigned bar=rpm>0 ? (rpm*128+4000)/8000 : 0;
            if (bar>128) bar=128;
            int approach=rpm-(static_cast<int>(targets[target])-2000);
            unsigned lights=rpm>=static_cast<int>(targets[target]) ? 13u :
                            approach>0 ? static_cast<unsigned>(approach)*12/2000 : 0u;
            assert(prefs.sweep(rpm)==bar && prefs.lights(rpm)==lights);
        }
    }
    prefs.diesel=true;
    assert(prefs.targetRpm()==4000 && prefs.scaleRpm()==6000);
    prefs.diesel_target=4500;
    prefs.metric=true;
    assert(prefs.save(path));
    ShiftPreferences restarted;
    assert(restarted.load(path) && restarted.diesel && restarted.metric);
    assert(restarted.diesel_target==4500 && restarted.gasoline_target==4);
    restarted.diesel=false;
    assert(restarted.targetRpm()==6700); // gasoline selection survives diesel use

    prefs.setDieselScale(0);
    assert(prefs.diesel_target==3900 && prefs.scaleRpm()==4000);
    for (unsigned i=0;i<100;++i) prefs.adjustDiesel(false);
    assert(prefs.diesel_target==1000);
    for (unsigned i=0;i<100;++i) prefs.adjustDiesel(true);
    assert(prefs.diesel_target==3900);
    prefs.setDieselScale(99);
    assert(prefs.diesel_scale==0 && prefs.valid());
    for (unsigned scale=0;scale<3;++scale) {
        prefs.setDieselScale(scale);
        unsigned maximum=prefs.scaleRpm();
        assert(prefs.sweep(maximum/2)==64 && prefs.sweep(maximum)==128);
        assert(prefs.sweep(INT_MAX)==128 && prefs.sweep(INT_MIN)==0);
        unsigned last=0;
        for (unsigned rpm=0;rpm<maximum+2000;++rpm) {
            unsigned next=prefs.sweep(rpm);
            assert(next>=last && next<=128);
            last=next;
        }
        for (unsigned target=1000;target<maximum;target+=100) {
            prefs.diesel_target=target;
            assert(prefs.valid() && prefs.lights(-1)==0 && prefs.lights(target-1000)==0);
            assert(prefs.lights(target-500)==6 && prefs.lights(target-1)==11);
            assert(prefs.lights(target)==13 && prefs.lights(INT_MAX)==13);
            assert(prefs.save(path));
            assert(restarted.load(path) && restarted.targetRpm()==target && restarted.scaleRpm()==maximum);
        }
    }
    const unsigned char bad[][9]={
        {'S',2,1,3,0,30,2,0,0}, {'S',1,2,3,0,30,2,0,0},
        {'S',1,1,5,0,30,2,0,0}, {'S',1,1,3,2,30,2,0,0},
        {'S',1,1,3,0,50,2,0,0}, {'S',1,1,3,0,30,3,0,0},
        {'S',1,1,3,0,30,0,0,0}, {'S',1,1,3,0,30,2,1,0}
    };
    for (unsigned i=0;i<sizeof bad/sizeof bad[0];++i) {
        assert(savePersistentBytes(path,bad[i],8));
        assert(!restarted.load(path) && restarted.targetRpm()==5900);
    }
    const unsigned char valid[]={'S',1,1,3,0,30,2,0,0};
    for (unsigned length=0;length<=9;++length) {
        if (length==8) continue;
        assert(savePersistentBytes(path,valid,length));
        assert(!restarted.load(path));
    }
    assert(!prefs.save("/nonexistent-idrive-directory/preferences"));
    prefs.diesel_target=1234;
    assert(!prefs.valid() && !prefs.save(path));
    unlink(path);
    puts("ShiftDeck legacy migration, profile persistence, display scaling, target bounds and corrupt preferences passed.");
}
