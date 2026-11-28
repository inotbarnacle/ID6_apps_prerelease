#ifndef DRIVE_FRAME_H
#define DRIVE_FRAME_H

#include <stdint.h>

enum { DRIVE_PORT = 42863, LAUNCH_DRIVE_PORT = 42864, DRIVE_STALE_MS = 750 };

struct drive_frame {
    uint64_t sampled_ms;
    int32_t rpm;
    int32_t kph;
    uint64_t speed_sampled_ms;
};

/* Full-screen v3 bundles carry independent request-start times. Older 16-byte
 * frames are rejected by the matching Library's size check. */
typedef char drive_frame_wire_size[sizeof(struct drive_frame)==24 ? 1 : -1];

#endif
