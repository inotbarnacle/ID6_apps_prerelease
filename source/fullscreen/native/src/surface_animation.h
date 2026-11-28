#ifndef ID6_SURFACE_ANIMATION_H
#define ID6_SURFACE_ANIMATION_H

#include "surface_draw.h"

enum { tach_transition_ms = 32 };

struct surface_animation {
    uint32_t page;
    unsigned start, target;
    uint64_t since;
};

static inline unsigned tach_animation_value(const struct surface_animation *a, uint64_t now)
{
    if (a->start == a->target || now >= a->since + tach_transition_ms)
        return a->target;
    int delta = (int)a->target - (int)a->start;
    const int remaining = tach_transition_ms - (int)(now-a->since);
    return a->target - delta*remaining*remaining/(tach_transition_ms*tach_transition_ms);
}

static inline void tach_animation_update(struct surface_animation *a,
                                        const struct surface_state *s, uint64_t now)
{
    unsigned target = surface_tach(s);
    if (s->page != a->page || !target || !a->target) {
        a->page = s->page;
        a->start = a->target = target;
        a->since = now;
    } else if (target != a->target) {
        a->start = tach_animation_value(a,now);
        a->target = target;
        a->since = now;
    }
}

#endif
