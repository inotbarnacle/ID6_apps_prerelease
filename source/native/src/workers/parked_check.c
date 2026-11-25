#define _POSIX_C_SOURCE 200809L
#include "gauge_core.h"
#include "diagnostic_lease.h"
#include "diag_link.h"
#include <math.h>

#include "hud_sample.h"

static int pressure(int fd, unsigned did, double *kpa) {
    const unsigned char request[] = {0x22, did >> 8, did & 0xff};
    const unsigned char prefix[] = {0x62, request[1], request[2]};
    unsigned char reply[5];
    unsigned negative;
    int count = diag_hsfz_exchange(fd, 0x12, request, 3, prefix, 3, reply, sizeof reply, 150, &negative);
    if (count == -1) return -1;
    return count == 5 && bd_decode_did(did, reply + 3, 2, kpa);
}

void hud_reader_init(hud_reader *reader) {
    memset(reader, 0, sizeof *reader);
    reader->fd = reader->lock = -1;
}

void hud_reader_close(hud_reader *reader) {
    release_gateway(&reader->fd, &reader->lock);
    hud_reader_init(reader);
}

static void pause_gateway(hud_reader *reader, unsigned wait_ms) {
    release_gateway(&reader->fd, &reader->lock);
    reader->retry_at = bd_mono_ms() + wait_ms;
}

int hud_reader_poll(hud_reader *reader, const char *host, hud_sample *sample, int require_park) {
    const unsigned char rpm[] = {1, 0x0c}, rpm_prefix[] = {0x41, 0x0c};
    const unsigned char speed[] = {1, 0x0d}, speed_prefix[] = {0x41, 0x0d};
    const unsigned char gear[] = {0x22, 0xda, 0x2e}, gear_prefix[] = {0x62, 0xda, 0x2e};
    unsigned char reply[5];
    unsigned negative;
    uint64_t now = bd_mono_ms();
    if (reader->fd < 0) {
        if (now < reader->retry_at) return 0;
        reader->lock = reserve_gateway();
        if (reader->lock < 0) return 0;
        reader->fd = diag_hsfz_connect(host, 6801);
        if (reader->fd < 0) {
            pause_gateway(reader, DIAGNOSTIC_RETRY_MS);
            return 0;
        }
        reader->lease_until = bd_mono_ms() + DIAGNOSTIC_LEASE_MS;
    }
    const int fd = reader->fd;
    int permitted = !require_park;
    if (!permitted &&
        diag_hsfz_exchange(fd, 0x12, speed, 2, speed_prefix, 2, reply, sizeof reply, 350, &negative) == 3 &&
        reply[2] == 0) {
        const int count = diag_hsfz_exchange(fd, 0x18, gear, 3, gear_prefix, 3, reply, sizeof reply, 350, &negative);
        if (count == 5) permitted = reply[3] == 0 && reply[4] == 6;
        else if (count == -2 && negative == 0x31) { /* M DCT: Park is in the DE31 status block */
            const unsigned char dkg[] = {0x22, 0xde, 0x31}, dkg_prefix[] = {0x62, 0xde, 0x31};
            unsigned char status[3 + BD_DKG_STATUS_SIZE];
            permitted = diag_hsfz_exchange(fd, 0x18, dkg, 3, dkg_prefix, 3, status, sizeof status, 350, &negative)
                            == (int)sizeof status &&
                        bd_dkg_parked(status + 3, BD_DKG_STATUS_SIZE);
        }
    }
    if (!permitted ||
        diag_hsfz_exchange(fd, 0x12, rpm, 2, rpm_prefix, 2, reply, sizeof reply, 180, &negative) != 4) {
        pause_gateway(reader, DIAGNOSTIC_RETRY_MS);
        return require_park ? -1 : 0;
    }
    sample->rpm = ((unsigned)reply[2] * 256 + reply[3]) / 4;
    sample->rpm_ms = bd_mono_ms();
    sample->gear = require_park ? 'P' : '-';

    if (sample->rpm_ms >= reader->boost_due) {
        double absolute, ambient;
        int result = pressure(fd, 0x4205, &absolute);
        if (result == 1) result = pressure(fd, 0x4201, &ambient);
        reader->boost_due = bd_mono_ms() + 450;
        if (result == 1) {
            reader->boost_tenths = (int)round((absolute - ambient) * 1.45037738);
            reader->boost_seen = bd_mono_ms();
        } else if (result < 0) {
            pause_gateway(reader, DIAGNOSTIC_YIELD_MS);
        } else {
            reader->boost_seen = 0;
        }
    }
    sample->have_boost = reader->boost_seen && bd_mono_ms() - reader->boost_seen < 1200;
    sample->boost_tenths = reader->boost_tenths;
    if (reader->fd >= 0 && gateway_should_yield(reader->lock, bd_mono_ms(), reader->lease_until))
        pause_gateway(reader, DIAGNOSTIC_YIELD_MS);
    return 1;
}

int hud_read(const char *host, hud_sample *sample, int require_park) {
    hud_reader reader;
    hud_reader_init(&reader);
    int valid = hud_reader_poll(&reader, host, sample, require_park);
    hud_reader_close(&reader);
    return valid == 1;
}
