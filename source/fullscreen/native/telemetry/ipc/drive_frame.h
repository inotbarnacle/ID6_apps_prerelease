#ifndef DRIVE_FRAME_H
#define DRIVE_FRAME_H

#include <stdint.h>

enum { DRIVE_PORT = 42863, LAUNCH_DRIVE_PORT = 42864, DRIVE_STALE_MS = 750 };

struct drive_frame {
    uint64_t sampled_ms;
    int32_t rpm;
    int32_t kph;
};

#endif
