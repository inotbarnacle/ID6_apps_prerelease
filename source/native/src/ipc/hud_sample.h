#ifndef HUD_SAMPLE_H
#define HUD_SAMPLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned rpm;
    uint64_t rpm_ms;
    int boost_tenths;
    int have_boost;
    char gear;
} hud_sample;

typedef struct {
    void *link;   /* struct diag_link, allocated on the first poll */
    uint64_t boost_due, boost_seen;
    int boost_tenths;
} hud_reader;

void hud_reader_init(hud_reader *reader);
void hud_reader_close(hud_reader *reader);
int hud_reader_poll(hud_reader *reader, const char *host, hud_sample *sample, int require_park);
int hud_read(const char *host, hud_sample *sample, int require_park);

#ifdef __cplusplus
}
#endif
#endif
