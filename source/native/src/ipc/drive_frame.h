#ifndef DRIVE_FRAME_H
#define DRIVE_FRAME_H

#include <stdint.h>

enum { DRIVE_PORT = 42863, LAUNCH_DRIVE_PORT = 42864, DRIVE_STALE_MS = 750, TPMS_STALE_MS = 60000 };

struct drive_frame {
    uint64_t sampled_ms;
    int32_t rpm;
    int32_t kph;
    uint64_t tpms_ms;    /* sender CLOCK_MONOTONIC of the last RDC read, 0 when never read */
    int32_t tpms_hpa[4]; /* FL, FR, RL, RR in hPa, -1 when unknown */
};

#endif
