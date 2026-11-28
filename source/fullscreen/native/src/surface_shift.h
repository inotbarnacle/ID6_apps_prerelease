#ifndef ID6_SURFACE_SHIFT_H
#define ID6_SURFACE_SHIFT_H
#include "surface_protocol.h"

static inline int surface_shift_valid(const struct surface_state *s) {
    if (s->magic!=surface_magic || s->page!=surface_shift || s->boost_signal[0]>1) return 0;
    unsigned ceiling=s->boost_level[0];
    if (!s->boost_signal[0])
        return ceiling==8000 && s->shift_target>=6000 && s->shift_target<=8000 && s->shift_target%100==0;
    return (ceiling==4000 || ceiling==5000 || ceiling==6000) &&
        s->shift_target>=1000 && s->shift_target<ceiling && s->shift_target%100==0;
}

static inline unsigned surface_shift_sample(const struct surface_state *s) {
    return surface_shift_valid(s) && s->boost_level[1]<=20001 ? s->boost_level[1] : 0;
}

static inline int surface_shift_light(const struct surface_state *s,unsigned light) {
    const unsigned sample=surface_shift_sample(s);
    if (!sample || light>=12) return 0;
    const unsigned rpm=sample-1,target=s->shift_target;
    if (!s->boost_signal[0]) return rpm>=target-(11u-light)*200u;
    if (rpm>=target) return 1;
    const unsigned remaining=target-rpm;
    return remaining<1000 && light<(1000-remaining)*12/1000;
}
#endif
