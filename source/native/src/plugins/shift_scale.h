#ifndef ID6_SHIFT_SCALE_H
#define ID6_SHIFT_SCALE_H

static inline unsigned shift_scale_max(bool n57) {
    return n57 ? 6000u : 8000u;
}

static inline unsigned shift_scale_step(int rpm, bool n57, unsigned steps) {
    const unsigned maximum=shift_scale_max(n57);
    if (rpm<=0) return 0;
    if (static_cast<unsigned>(rpm)>=maximum) return steps;
    return (static_cast<unsigned>(rpm)*steps+maximum/2u)/maximum;
}

static inline bool shift_target_allowed(unsigned index, bool n57) {
    return index<8u && (!n57 || index>=5u);
}

#endif
