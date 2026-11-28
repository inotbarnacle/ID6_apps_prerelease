#include "shift_state.h"
#include <assert.h>
#include <limits.h>

static void same(const char* path,const unsigned char* expected,size_t length) {
    unsigned char data[16];FILE* file=fopen(path,"rb");assert(file);
    assert(fread(data,1,sizeof data,file)==length && !ferror(file));
    assert(fclose(file)==0 && !memcmp(data,expected,length));
}

int main() {
    char directory[]="/tmp/full-shift-state.XXXXXX";assert(mkdtemp(directory));
    char gasoline[128],profile[128];
    snprintf(gasoline,sizeof gasoline,"%s/gasoline",directory);
    snprintf(profile,sizeof profile,"%s/profile",directory);
    FullShiftPreferences prefs;
    assert(prefs.valid() && prefs.metric && prefs.targetRpm()==6500);
    for (unsigned target=0;target<21;++target) for (unsigned mph=0;mph<2;++mph) {
        const unsigned char record[]={'G',1,static_cast<unsigned char>(target),static_cast<unsigned char>(mph)};
        assert(savePersistentBytes(gasoline,record,sizeof record));
        assert(prefs.load(gasoline,profile) && prefs.metric==!mph);
        assert(prefs.targetRpm()==6000+target*100 && !prefs.diesel);
        assert(prefs.saveGasoline(gasoline));same(gasoline,record,sizeof record);
        assert(access(profile,F_OK)!=0);
    }
    const unsigned char original[]={'G',1,20,1};
    for (unsigned scale=0;scale<3;++scale) {
        for (unsigned target=1000;target<4000+scale*1000;target+=100) {
            prefs.diesel=true;prefs.diesel_scale=scale;prefs.diesel_target=target;
            assert(prefs.saveProfile(profile));same(gasoline,original,sizeof original);
            FullShiftPreferences restarted;
            assert(restarted.load(gasoline,profile) && restarted.diesel && !restarted.metric);
            assert(restarted.targetRpm()==target && restarted.scaleRpm()==4000+scale*1000);
            const unsigned char record[]={'S',2,1,static_cast<unsigned char>((target-1000)/100),static_cast<unsigned char>(scale)};
            same(profile,record,sizeof record);
            restarted.metric=true;assert(restarted.saveGasoline(gasoline));
            same(profile,record,sizeof record);assert(savePersistentBytes(gasoline,original,sizeof original));
        }
    }
    // A full-screen G1 must never be interpreted as a standard five-preset file.
    const unsigned char legacy[]={'S',1,1,3,0,20,2,0};
    assert(savePersistentBytes(gasoline,legacy,sizeof legacy));unlink(profile);
    assert(!prefs.load(gasoline,profile) && prefs.targetRpm()==6500 && prefs.metric && !prefs.diesel);
    same(gasoline,legacy,sizeof legacy);
    const unsigned char valid[]={'S',2,1,30,2,0};
    for (unsigned length=0;length<=6;++length) {
        if (length==5) continue;
        assert(savePersistentBytes(profile,valid,length));
        assert(!prefs.load(gasoline,profile) && !prefs.diesel && prefs.targetRpm()==6500);
    }
    const unsigned char invalid[][5]={{'S',2,2,30,2},{'S',2,1,30,0},{'S',2,1,0,3},{'S',1,1,30,2}};
    for (unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        assert(savePersistentBytes(profile,invalid[i],5));
        assert(!prefs.load(gasoline,profile) && !prefs.diesel);
    }
    assert(!prefs.saveProfile("/nonexistent-idrive-full-shift/settings"));
    assert(!prefs.saveGasoline("/nonexistent-idrive-full-shift/settings"));
    unlink(gasoline);unlink(profile);assert(rmdir(directory)==0);

    FullShiftState state;drive_frame frame={1000,0,0,1000};
    state.reset(1000);assert(state.accept(frame,1000));
    assert(state.rpm(1000)==0 && state.kph(1000)==0);
    assert(state.rpm(1749)==0 && state.rpm(1750)==-1);
    assert(!state.accept(frame,1100)); // duplicate never extends freshness
    frame.sampled_ms=999;assert(!state.accept(frame,1100));
    frame.sampled_ms=1101;assert(!state.accept(frame,1100));
    frame.sampled_ms=frame.speed_sampled_ms=1100;frame.rpm=20000;frame.kph=255;
    assert(state.accept(frame,1100) && state.rpm(1100)==20000 && state.kph(1100)==255);
    frame.sampled_ms=0;assert(state.accept(frame,1100) && state.rpm(1100)==-1);
    frame.sampled_ms=1100;assert(!state.accept(frame,1200)); // invalidation retains ordering
    frame.sampled_ms=frame.speed_sampled_ms=1200;frame.rpm=INT_MAX;frame.kph=256;
    assert(state.accept(frame,1200) && state.rpm(1200)==-1 && state.kph(1200)==-1);
    frame.sampled_ms=frame.speed_sampled_ms=1300;frame.rpm=-1;frame.kph=0;
    assert(state.accept(frame,1300) && state.rpm(1300)==-1 && state.kph(1300)==0);
    state.reset(1400);assert(!state.accept(frame,1400));
    frame.sampled_ms=frame.speed_sampled_ms=1400;frame.rpm=2000;frame.kph=80;
    assert(state.accept(frame,1400));
    frame.sampled_ms=2100;frame.rpm=3000;assert(state.accept(frame,2100));
    assert(state.rpm(2150)==3000 && state.kph(2149)==80 && state.kph(2150)==-1);
    frame.sampled_ms=2150;frame.speed_sampled_ms=2151;assert(state.accept(frame,2150));
    assert(state.rpm(2150)==3000 && state.kph(2150)==-1);
    frame.sampled_ms=frame.speed_sampled_ms=2200;frame.kph=90;
    assert(state.accept(frame,2200) && state.kph(2200)==90);
    frame.sampled_ms=2250;frame.speed_sampled_ms=0;assert(state.accept(frame,2250));
    frame.sampled_ms=2300;frame.speed_sampled_ms=2200;assert(state.accept(frame,2300));
    assert(state.kph(2300)==-1); // invalidated speed cannot revive from a repeated cache time
    puts("Full-screen Shift: legacy mph bytes, independent S2, read-only loads, invalid records, zero/stale/domain/order checks passed.");
}
