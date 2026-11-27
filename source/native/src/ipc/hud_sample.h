#ifndef HUD_SAMPLE_H
#define HUD_SAMPLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "diag_link.h"

enum { HUD_RPM_STALE_MS=1100, HUD_BOOST_STALE_MS=1200 };
typedef struct {
    unsigned rpm;
    uint64_t rpm_ms;
    int boost_tenths;
    int have_boost;
    uint64_t boost_ms;
    char gear;
    int n57;
} hud_sample;

static inline int hud_boost_fresh(const hud_sample *s, uint64_t now) {
    return s->have_boost && s->boost_ms && s->boost_ms<=now && now-s->boost_ms<HUD_BOOST_STALE_MS;
}

typedef struct {
    int fd, lock;
    uint64_t lease_until, retry_at, boost_due, boost_seen;
    int boost_tenths;
    unsigned n57_index;
    int config_error;
    struct diag_link telemetry;
    int telemetry_initialized;
    char telemetry_host[16];
} hud_reader;

void hud_reader_init(hud_reader *reader);
void hud_reader_close(hud_reader *reader);
int hud_reader_poll(hud_reader *reader, const char *host, hud_sample *sample, int require_park);
int hud_read(const char *host, hud_sample *sample, int require_park);

#ifdef __cplusplus
}
#endif
#endif
