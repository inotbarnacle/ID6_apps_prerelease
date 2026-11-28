#ifndef ID6_FULL_SHIFT_STATE_H
#define ID6_FULL_SHIFT_STATE_H

#include "drive_frame.h"
#include "../../../native/src/plugins/shift_preferences.h"
#include <string.h>

inline bool saveFullShiftRecord(const char* path,const unsigned char* data,size_t count) {
#ifdef ID6_EMULATOR
    // Preserve the existing emulator path: QNX shared memory has no atomic rename.
    FILE* file=fopen(path,"wb");
    if (!file) return false;
    const bool saved=fwrite(data,1,count,file)==count;
    return fclose(file)==0 && saved;
#else
    return savePersistentBytes(path,data,count);
#endif
}

struct FullShiftPreferences : ShiftPreferences {
    FullShiftPreferences() { metric=true; } // Full-screen defaults to km/h.
    bool load(const char* gasoline,const char* profile) {
        FullShiftPreferences next;
        unsigned target=next.gasoline_target;
        bool mph=false;
        bool loaded=loadGaugePreferences(gasoline,GASOLINE_TARGETS,target,mph);
        if (loaded) { next.gasoline_target=target;next.metric=!mph; }
        unsigned char data[8];size_t count=0;
        if (record(profile,data,count)==1 && decodeProfile(data,count,next)) loaded=true;
        *this=next;
        return loaded;
    }
    bool saveGasoline(const char* path) const {
        if (!valid()) return false;
        // Full-screen G1 byte 3 means mph; standard ShiftDeck G1 means metric.
        const unsigned char data[]={'G',1,static_cast<unsigned char>(gasoline_target),
                                    static_cast<unsigned char>(!metric)};
        return saveFullShiftRecord(path,data,sizeof data);
    }
    bool saveProfile(const char* path) const {
        unsigned char data[5];
        return encodeProfile(data) && saveFullShiftRecord(path,data,sizeof data);
    }
};

class FullShiftState {
    drive_frame frame_;
    uint64_t opened_,newest_,newest_speed_;
public:
    FullShiftState() { reset(0); }
    void reset(uint64_t now) {
        memset(&frame_,0,sizeof frame_);
        frame_.rpm=frame_.kph=-1;
        opened_=now;newest_=newest_speed_=0;
    }
    bool accept(const drive_frame& next,uint64_t now) {
        if (!next.sampled_ms) { frame_.sampled_ms=frame_.speed_sampled_ms=0;return true; }
        if (next.sampled_ms<opened_ || next.sampled_ms>now || next.sampled_ms<=newest_) return false;
        newest_=next.sampled_ms;
        frame_.sampled_ms=next.sampled_ms;frame_.rpm=next.rpm;
        if (!next.speed_sampled_ms) frame_.speed_sampled_ms=0;
        else if (next.speed_sampled_ms>=opened_ && next.speed_sampled_ms<=now &&
                 (next.speed_sampled_ms>newest_speed_ ||
                  (next.speed_sampled_ms==frame_.speed_sampled_ms && next.kph==frame_.kph))) {
            // Cached speed may repeat its original time while RPM advances.
            newest_speed_=next.speed_sampled_ms;
            frame_.speed_sampled_ms=next.speed_sampled_ms;frame_.kph=next.kph;
        }
        return true;
    }
    bool fresh(uint64_t now) const {
        return frame_.sampled_ms && frame_.sampled_ms<=now && now-frame_.sampled_ms<DRIVE_STALE_MS;
    }
    int rpm(uint64_t now) const {
        // Retain the renderer's existing 20,000 RPM input bound, not an ECU limit.
        return fresh(now) && frame_.rpm>=0 && frame_.rpm<=20000 ? frame_.rpm : -1;
    }
    int kph(uint64_t now) const {
        // The unchanged worker reads the unsigned-byte standard speed PID 0D.
        return frame_.speed_sampled_ms && frame_.speed_sampled_ms<=now &&
            now-frame_.speed_sampled_ms<DRIVE_STALE_MS && frame_.kph>=0 && frame_.kph<=255 ? frame_.kph : -1;
    }
};
#endif
