#ifndef ID6_FULL_BOOST_STATE_H
#define ID6_FULL_BOOST_STATE_H
#include "../../../native/src/common/boost_defaults.h"
#include "fullscreen_preferences.h"
#include "../../../native/src/plugins/boost_defaults_mode.h"
#include <string.h>
#include <sys/types.h>

struct FullBoostDefaultsMode : BoostDefaultsMode {
    bool save(const char* path) const {
        if (!*path) return false;
        const unsigned char bytes[]={'D',1,static_cast<unsigned char>(automatic)};
        return saveFullScreenRecord(path,bytes,sizeof bytes);
    }
};

class FullBoostState {
    bd_frame frame_;
    uint64_t opened_,received_;
    bd_mask known_,supported_;
    unsigned char slots_[BD_DEFAULT_SLOTS];
public:
    FullBoostState() { reset(0); }
    void reset(uint64_t now) {
        memset(&frame_,0,sizeof frame_);
        opened_=now;received_=0;known_=supported_=0;
        bd_diesel_slots(0,slots_);
    }
    bool accept(const char* data,size_t length,uint64_t now,bool automatic,uint64_t token,pid_t worker) {
        bd_frame next;bd_cap_frame report;
        if (automatic) {
            if (worker<=0 || !bd_auto_parse(data,length,&report,&next) || report.token!=token ||
                report.worker_pid!=static_cast<uint32_t>(worker) || (known_ & ~report.known) ||
                ((supported_ ^ report.supported) & known_)) return false;
        } else if (!bd_parse(data,length,&next)) return false;
        if (next.demo || !next.sent_ms || next.sent_ms<opened_ || next.sent_ms>now ||
            next.sent_ms<received_ || now-next.sent_ms>=BD_STALE_MS) return false;
        frame_=next;received_=next.sent_ms;
        if (automatic) {
            known_=report.known;supported_=report.supported;
            bd_diesel_slots(supported_,slots_);
        }
        return true;
    }
    bool fresh(unsigned signal,uint64_t now) const {
        return signal<BD_COUNT && received_ && received_<=now &&
            frame_.age_ms[signal]<=frame_.sent_ms && frame_.sent_ms-frame_.age_ms[signal]>=opened_ &&
            bd_is_fresh(&frame_,signal,now-received_);
    }
    unsigned automaticSignal(unsigned slot) const {
        return slot<BD_DEFAULT_SLOTS ? static_cast<unsigned>(slots_[slot]) : static_cast<unsigned>(BD_EMPTY_SLOT);
    }
    const bd_frame& frame() const { return frame_; }
    bd_mask known() const { return known_; }
    bd_mask supported() const { return supported_; }
};
#endif
