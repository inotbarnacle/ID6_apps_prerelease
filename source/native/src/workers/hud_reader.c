#define _POSIX_C_SOURCE 200809L
#include "gauge_core.h"
#include "diagnostic_lease.h"
#include "diag_link.h"
#include <math.h>
#include <stdlib.h>

#include "hud_sample.h"

static int pressure(struct diag_link *link, unsigned did, double *kpa) {
    const unsigned char request[] = {0x22, did >> 8, did & 0xff};
    const unsigned char prefix[] = {0x62, request[1], request[2]};
    unsigned char reply[5];
    unsigned negative;
    int count = diag_link_request(link, 0x12, request, 3, prefix, 3, reply, sizeof reply, 150, &negative);
    if (count == -1) return -1;
    return count == 5 && bd_decode_did(did, reply + 3, 2, kpa);
}

void hud_reader_init(hud_reader *reader) {
    memset(reader, 0, sizeof *reader);
}

void hud_reader_close(hud_reader *reader) {
    if (reader->link) diag_link_close(reader->link);
    free(reader->link);
    hud_reader_init(reader);
}

static void pause_gateway(struct diag_link *link, unsigned wait_ms) {
    diag_link_drop(link);
    link->retry_at = bd_mono_ms() + wait_ms;
}

int hud_reader_poll(hud_reader *reader, const char *host, hud_sample *sample, int require_park) {
    const unsigned char rpm[] = {1, 0x0c}, rpm_prefix[] = {0x41, 0x0c};
    const unsigned char speed[] = {1, 0x0d}, speed_prefix[] = {0x41, 0x0d};
    const unsigned char gear[] = {0x22, 0xda, 0x2e}, gear_prefix[] = {0x62, 0xda, 0x2e};
    unsigned char reply[5];
    unsigned negative;
    struct diag_link *link = reader->link;
    if (!link) {
        struct diag_link_cfg cfg = {host, 6801, DIAG_AUTO, DIAG_LOOP, "hudreader", 1};
        if (!(link = reader->link = calloc(1, sizeof *link))) return 0;
        diag_link_init(link, &cfg);
    }
    if (!diag_link_up(link)) return 0;
    const int permitted = !require_park ||
        (diag_link_request(link, 0x12, speed, 2, speed_prefix, 2, reply, sizeof reply, 350, &negative) == 3 &&
         reply[2] == 0 &&
         diag_link_request(link, 0x18, gear, 3, gear_prefix, 3, reply, sizeof reply, 350, &negative) == 5 &&
         reply[3] == 0 && reply[4] == 6);
    if (!permitted ||
        diag_link_request(link, 0x12, rpm, 2, rpm_prefix, 2, reply, sizeof reply, 180, &negative) != 4) {
        pause_gateway(link, DIAGNOSTIC_RETRY_MS);
        return require_park ? -1 : 0;
    }
    sample->rpm = ((unsigned)reply[2] * 256 + reply[3]) / 4;
    sample->rpm_ms = bd_mono_ms();
    sample->gear = require_park ? 'P' : '-';

    if (sample->rpm_ms >= reader->boost_due) {
        double absolute, ambient;
        int result = pressure(link, 0x4205, &absolute);
        if (result == 1) result = pressure(link, 0x4201, &ambient);
        reader->boost_due = bd_mono_ms() + 450;
        if (result == 1) {
            reader->boost_tenths = (int)round((absolute - ambient) * 1.45037738);
            reader->boost_seen = bd_mono_ms();
        } else if (result < 0) {
            pause_gateway(link, DIAGNOSTIC_YIELD_MS);
        } else {
            reader->boost_seen = 0;
        }
    }
    sample->have_boost = reader->boost_seen && bd_mono_ms() - reader->boost_seen < 1200;
    sample->boost_tenths = reader->boost_tenths;
    (void)diag_link_should_yield(link, bd_mono_ms());
    return 1;
}

int hud_read(const char *host, hud_sample *sample, int require_park) {
    hud_reader reader;
    hud_reader_init(&reader);
    int valid = hud_reader_poll(&reader, host, sample, require_park);
    hud_reader_close(&reader);
    return valid == 1;
}
