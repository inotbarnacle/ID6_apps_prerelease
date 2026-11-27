#ifndef DRIVE_FRAME_H
#define DRIVE_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum { DRIVE_PORT = 42863, LAUNCH_DRIVE_PORT = 42864, DRIVE_STALE_MS = 750 };

struct drive_frame {
    uint64_t sampled_ms;
    int32_t rpm;
    int32_t kph;
};

/* N57 uses fractional km/h. Keep legacy frames readable without rounding motion to zero. */
#define DRIVE_N57_MAGIC UINT64_C(0xffffffff31454444) /* DDE1; legacy timestamp checks reject it. */
struct drive_n57_frame {
    uint64_t magic;
    uint64_t sampled_ms;
    int32_t rpm, mkph;
};

struct drive_sample {
    uint64_t sampled_ms;
    int32_t rpm, mkph;
    int n57;
};

static inline int drive_parse(const void *data, size_t size, struct drive_sample *out) {
    struct drive_sample sample;
    if (size==sizeof(struct drive_frame)) {
        struct drive_frame f;
        memcpy(&f,data,sizeof f);
        if (f.rpm < -1 || f.rpm > 16383 || f.kph < -1 || f.kph > 255) return 0;
        sample.sampled_ms=f.sampled_ms; sample.rpm=f.rpm;
        sample.mkph=f.kph < 0 ? -1 : f.kph*1000; sample.n57=0;
    } else if (size==sizeof(struct drive_n57_frame)) {
        struct drive_n57_frame f;
        memcpy(&f,data,sizeof f);
        if (f.magic!=DRIVE_N57_MAGIC || f.rpm < -1 || f.rpm > 6000 ||
            f.mkph < -1 || f.mkph > 300000) return 0;
        sample.sampled_ms=f.sampled_ms; sample.rpm=f.rpm; sample.mkph=f.mkph; sample.n57=1;
    } else return 0;
    *out=sample;
    return 1;
}

static inline int drive_display_speed(int32_t mkph, int metric) {
    if (mkph<0) return -1;
    return metric ? (mkph+500)/1000 : (int)(((int64_t)mkph*621371+500000000)/1000000000);
}

#endif
