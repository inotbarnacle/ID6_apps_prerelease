#ifndef SHIFT_PREFERENCES_H
#define SHIFT_PREFERENCES_H

#include "gauge_preferences.h"
#include "shift_assets.h"

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

    bool load(const char* path) {
        FILE* file = fopen(path,"rb");
        if (!file) return false;
        unsigned char data[8];
        const size_t count = fread(data,1,sizeof data,file);
        bool complete = !ferror(file);
        if (complete && !feof(file)) complete = fgetc(file) == EOF && !ferror(file);
        fclose(file);
        if (!complete) return false;
        ShiftPreferences candidate;
        if (count == 4 && data[0] == 'G' && data[1] == 1 && data[2] < GASOLINE_TARGETS && data[3] <= 1) {
            candidate.gasoline_target = data[2];
            candidate.metric = data[3] != 0;
        } else if (count == 8 && data[0] == 'S' && data[1] == 1 && data[2] <= 1 &&
                   data[4] <= 1 && data[7] == 0) {
            candidate.diesel = data[2] != 0;
            candidate.gasoline_target = data[3];
            candidate.metric = data[4] != 0;
            candidate.diesel_target = MIN_TARGET+static_cast<unsigned>(data[5])*STEP;
            candidate.diesel_scale = data[6];
        } else return false;
        if (!candidate.valid()) return false;
        *this = candidate;
        return true;
    }
    bool save(const char* path) const {
        if (!valid()) return false;
        const unsigned char data[] = {'S',1,static_cast<unsigned char>(diesel),
            static_cast<unsigned char>(gasoline_target),static_cast<unsigned char>(metric),
            static_cast<unsigned char>((diesel_target-MIN_TARGET)/STEP),
            static_cast<unsigned char>(diesel_scale),0};
        return savePersistentBytes(path,data,sizeof data);
    }
};

#endif
