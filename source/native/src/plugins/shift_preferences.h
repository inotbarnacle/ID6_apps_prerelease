#ifndef SHIFT_PREFERENCES_H
#define SHIFT_PREFERENCES_H

#include "gauge_preferences.h"
#include "shift_assets.h"
#include <errno.h>

// Display/advisory choices, never ECU limits or a claimed BMW redline.
struct ShiftPreferences {
    enum { MIN_TARGET = 1000, STEP = 100, SCALE_COUNT = 3, SWEEP_STEPS = 128,
           GASOLINE_TARGETS = sizeof kShiftTargets/sizeof kShiftTargets[0] };
    unsigned gasoline_target, diesel_target, diesel_scale;
    bool diesel, metric;

    ShiftPreferences() : gasoline_target(kDefaultTarget), diesel_target(4000), diesel_scale(2),
                         diesel(false), metric(false) {}

    unsigned scaleRpm() const { return diesel ? 4000 + diesel_scale*1000 : 8000; }
    unsigned targetRpm() const {
        return diesel ? diesel_target : kShiftTargets[gasoline_target];
    }
    bool valid() const {
        return gasoline_target < GASOLINE_TARGETS && diesel_scale < SCALE_COUNT &&
               diesel_target >= MIN_TARGET && diesel_target < 4000+diesel_scale*1000 &&
               diesel_target/STEP*STEP == diesel_target;
    }
    void adjustDiesel(bool increase) {
        if (increase && diesel_target+STEP < 4000+diesel_scale*1000) diesel_target += STEP;
        else if (!increase && diesel_target > MIN_TARGET) diesel_target -= STEP;
    }
    void setDieselScale(unsigned index) {
        if (index >= SCALE_COUNT) return;
        diesel_scale = index;
        const unsigned limit = 4000+index*1000;
        if (diesel_target >= limit) diesel_target = limit-STEP;
    }
    unsigned sweep(int rpm) const {
        if (rpm <= 0) return 0;
        if (static_cast<unsigned>(rpm) >= scaleRpm()) return SWEEP_STEPS;
        const unsigned scaled = static_cast<unsigned>(rpm)*SWEEP_STEPS;
        // Constant divisors avoid a new runtime ARM division-helper import.
        if (!diesel) return (scaled+4000)/8000;
        if (diesel_scale == 0) return (scaled+2000)/4000;
        if (diesel_scale == 1) return (scaled+2500)/5000;
        return (scaled+3000)/6000;
    }
    unsigned lights(int rpm) const {
        if (rpm < 0) return 0;
        if (static_cast<unsigned>(rpm) >= targetRpm()) return 13;
        const unsigned lead = diesel ? 1000 : 2000;
        const unsigned remaining = targetRpm()-static_cast<unsigned>(rpm);
        if (remaining >= lead) return 0;
        return diesel ? (lead-remaining)*12/1000 : (lead-remaining)*12/2000;
    }

    // 0 means absent; an unreadable/malformed new sidecar must not revive
    // an older profile choice from a legacy S1 record.
    static int record(const char* path, unsigned char data[8], size_t& count) {
        FILE* file = fopen(path,"rb");
        if (!file) return errno==ENOENT ? 0 : -1;
        count = fread(data,1,8,file);
        bool complete = !ferror(file);
        if (complete && !feof(file)) complete = fgetc(file) == EOF && !ferror(file);
        fclose(file);
        return complete ? 1 : -1;
    }

    bool load(const char* gasoline, const char* legacy, const char* profile) {
        ShiftPreferences candidate;
        bool loaded=loadGaugePreferences(gasoline,GASOLINE_TARGETS,
                                         candidate.gasoline_target,candidate.metric);
        const bool current=loaded;
        static const unsigned converted[]={0,0,3,5,7}; // upstream's five-preset migration
        unsigned char data[8];
        size_t count=0;
        bool legacy_profile=false;
        ShiftPreferences previous;
        if (record(legacy,data,count)==1) {
            if (count==4 && data[0]=='G' && data[1]==1 && data[2]<5 && data[3]<=1) {
                previous.gasoline_target=converted[data[2]];
                previous.metric=data[3]!=0;
                if (!current) {
                    candidate.gasoline_target=previous.gasoline_target;
                    candidate.metric=previous.metric;
                    loaded=true;
                }
            } else if (count==8 && data[0]=='S' && data[1]==1 && data[2]<=1 &&
                       data[3]<5 && data[4]<=1 && data[7]==0) {
                previous.gasoline_target=converted[data[3]];
                previous.metric=data[4]!=0;
                previous.diesel=data[2]!=0;
                previous.diesel_target=MIN_TARGET+static_cast<unsigned>(data[5])*STEP;
                previous.diesel_scale=data[6];
                legacy_profile=previous.valid();
                if (legacy_profile && !current) {
                    candidate.gasoline_target=previous.gasoline_target;
                    candidate.metric=previous.metric;
                    loaded=true;
                }
            }
        }
        const int profile_status=record(profile,data,count);
        if (profile_status==1 && count==5 && data[0]=='S' && data[1]==2 && data[2]<=1) {
            ShiftPreferences next=candidate;
            next.diesel=data[2]!=0;
            next.diesel_target=MIN_TARGET+static_cast<unsigned>(data[3])*STEP;
            next.diesel_scale=data[4];
            if (next.valid()) { candidate=next; loaded=true; }
        } else if (profile_status==0 && legacy_profile) {
            candidate.diesel=previous.diesel;
            candidate.diesel_target=previous.diesel_target;
            candidate.diesel_scale=previous.diesel_scale;
            loaded=true;
        }
        if (!loaded) return false;
        *this = candidate;
        return true;
    }
    bool saveGasoline(const char* path) const {
        if (!valid()) return false;
        const unsigned char data[]={'G',1,static_cast<unsigned char>(gasoline_target),
                                   static_cast<unsigned char>(metric)};
        return savePersistentBytes(path,data,sizeof data);
    }
    bool saveProfile(const char* path) const {
        if (!valid()) return false;
        const unsigned char data[]={'S',2,static_cast<unsigned char>(diesel),
            static_cast<unsigned char>((diesel_target-MIN_TARGET)/STEP),
            static_cast<unsigned char>(diesel_scale)};
        return savePersistentBytes(path,data,sizeof data);
    }
};

#endif
