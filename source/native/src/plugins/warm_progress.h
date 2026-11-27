#ifndef WARM_PROGRESS_H
#define WARM_PROGRESS_H

#include "warm_frame.h"

static const double kWarmTarget[WARM_COUNT]={85,75,40};
static const double kWarmLower[WARM_COUNT]={81,70,35};

class WarmProgress {
    uint64_t anchor_[WARM_COUNT];
    double baseline_[WARM_COUNT], rate_[WARM_COUNT];
    bool reached_[WARM_COUNT];
public:
    WarmProgress() { reset(); }

    void reset() {
        for (unsigned i=0;i<WARM_COUNT;++i) {
            reached_[i]=false;
            anchor_[i]=0;
            baseline_[i]=rate_[i]=0;
        }
    }

    bool targetReached(unsigned fluid) const { return fluid<WARM_COUNT && reached_[fluid]; }

    // Positive estimates require a rising trend for every fluid still below its target.
    int update(const warm_frame& frame, unsigned valid) {
        bool all=true, estimate=true;
        double longest=0;
        for (unsigned i=0;i<WARM_COUNT;++i) {
            if (!(valid & (1u<<i))) {
                reached_[i]=false;
                anchor_[i]=0;
                rate_[i]=0;
                all=estimate=false;
                continue;
            }
            const uint64_t stamp=frame.sample[i];
            const double value=frame.celsius[i];
            if (!anchor_[i] || stamp<anchor_[i] || stamp-anchor_[i]>45000) {
                anchor_[i]=stamp;
                baseline_[i]=value;
                rate_[i]=0;
            } else if (stamp-anchor_[i]>=15000) {
                rate_[i]=(value-baseline_[i])*1000/(stamp-anchor_[i]);
                anchor_[i]=stamp;
                baseline_[i]=value;
            }
            if (value>=kWarmTarget[i]) reached_[i]=true;
            else if (value<kWarmLower[i]) reached_[i]=false;
            if (!reached_[i]) all=false;
            if (reached_[i]) rate_[i]=0;
            else {
                if (rate_[i]<0.015) estimate=false;
                else {
                    const double seconds=(kWarmTarget[i]-value)/rate_[i];
                    if (seconds>longest) longest=seconds;
                }
            }
        }
        if (all) return 0;
        return estimate && longest>0 && longest<=1800 ? static_cast<int>(longest+0.99) : -1;
    }
};

#endif
