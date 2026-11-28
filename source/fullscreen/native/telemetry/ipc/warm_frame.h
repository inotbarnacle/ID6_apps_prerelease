#ifndef WARM_FRAME_H
#define WARM_FRAME_H

#include <stdint.h>

enum { WARM_PORT=42862, WARM_STALE_MS=4500 };
enum { WARM_OIL, WARM_COOLANT, WARM_TRANS, WARM_COUNT };

struct warm_frame {
    uint64_t sample[WARM_COUNT];
    double celsius[WARM_COUNT];
};

#endif
