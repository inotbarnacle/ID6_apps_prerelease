#ifndef ID6_FULL_WARM_STATE_H
#define ID6_FULL_WARM_STATE_H

#include "surface_protocol.h"
// Use the same measured trend and hysteresis model as the standard app.
// Full-screen source builds require this repository's standard native source.
#include "../../../native/src/plugins/warm_progress.h"
#include <string.h>

class FullWarmState {
    warm_frame frame_;
    uint64_t opened_, newest_[WARM_COUNT];
    WarmProgress progress_;
    unsigned valid_;
    int eta_;
public:
    FullWarmState() { reset(0); }
    void reset(uint64_t now) {
        memset(&frame_,0,sizeof frame_);
        memset(newest_,0,sizeof newest_);
        opened_=now;
        valid_=0;
        eta_=-1;
        progress_.reset();
    }
    void accept(const warm_frame& candidate, uint64_t now) {
        for (unsigned i=0;i<WARM_COUNT;++i) {
            const uint64_t sample=candidate.sample[i];
            if (!sample) frame_.sample[i]=0;
            else if (sample>=opened_ && sample<=now && sample>newest_[i]) {
                newest_[i]=frame_.sample[i]=sample;
                frame_.celsius[i]=candidate.celsius[i];
            }
        }
        update(now);
    }
    void update(uint64_t now) {
        valid_=0;
        for (unsigned i=0;i<WARM_COUNT;++i)
            if (frame_.sample[i] && frame_.sample[i]>=opened_ && frame_.sample[i]<=now &&
                now-frame_.sample[i]<WARM_STALE_MS &&
                frame_.celsius[i]>=-48 && frame_.celsius[i]<=180)
                valid_|=1u<<i;
        eta_=progress_.update(frame_,valid_);
    }
    unsigned valid() const { return valid_; }
    int eta() const { return eta_; }
    double celsius(unsigned fluid) const { return fluid<WARM_COUNT ? frame_.celsius[fluid] : 0; }
    unsigned target(unsigned fluid) const {
        return fluid>=WARM_COUNT || !(valid_&(1u<<fluid)) ? surface_warm_unknown :
            progress_.targetReached(fluid) ? surface_warm_met : surface_warm_pending;
    }
    unsigned level(unsigned fluid) const {
        if (target(fluid)==surface_warm_unknown) return 0;
        const double fraction=frame_.celsius[fluid]/kWarmTarget[fluid];
        return fraction>=1 ? 1000u : fraction>0 ? static_cast<unsigned>(fraction*1000+0.5) : 0u;
    }
};
#endif
