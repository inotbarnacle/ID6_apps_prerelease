/* src/bm3.c against the cases of cauliflowermossgrape's test_bm3_protocol.js (markerDetectionChecks and the
 * per-profile loop), plus the request bytes it asserts. */
#include "bm3.h"
#include "bm3_profiles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);         \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static const char *captured_hex =
    "1141F2144D88227FC9D6EBF6C0CEE74104BFF6652CF4A3F310CDC33BB4EF1387D7DA9"
    "11E2F2C0A906D91D631DE69E83FB14CF2A22CEA7161831C7D085133B3CC";

/* BM3_, the captured 64-byte block, ~|~, 30 underscores, #token, NUL, the rest 0xFF. */
static void binary_marker(unsigned char *m, const char *token) {
    size_t i, at = 0;
    memset(m, 0xFF, BM3_MARKER_SIZE);
    memcpy(m, "BM3_", 4);
    at = 4;
    for (i = 0; i < 64; ++i) {
        unsigned value;
        sscanf(captured_hex + 2 * i, "%2X", &value);
        m[at++] = (unsigned char)value;
    }
    memcpy(m + at, "~|~", 3);
    at += 3;
    memset(m + at, '_', 30);
    at += 30;
    m[at++] = '#';
    memcpy(m + at, token, strlen(token));
    at += strlen(token);
    m[at] = 0;
}

/* The F-series MEVD17 layout, as read at 0977FE4C on an F20 M140i: #DST, then the binary layout above with the stock
 * ROM's tag before the bootmod3 one. The 64 bytes are the MG1 capture's, not the car's. */
static void mevd17_marker(unsigned char *m, const char *stock, const char *token) {
    size_t at = 0;
    unsigned char inner[BM3_MARKER_SIZE];
    binary_marker(inner, "X");
    memset(m, 0xFF, BM3_MARKER_SIZE);
    memcpy(m, "#DST", 4);
    memcpy(m + 4, inner, 4 + 64 + 3); /* BM3_, the block, ~|~ */
    at = 4 + 4 + 64 + 3;
    memset(m + at, '_', 4);
    at += 4;
    at += (size_t)sprintf((char *)m + at, "#%s", stock);
    memset(m + at, '_', 6);
    at += 6;
    at += (size_t)sprintf((char *)m + at, "#%s", token);
    m[at] = 0;
}

/* The S58 layout (G87, issue #175): BM3_, synthetic bytes without '#', the only '#' at byte 101, the tag, NUL. */
static void hash_marker(unsigned char *m, const char *tag) {
    size_t i;
    memset(m, 0, BM3_MARKER_SIZE);
    memcpy(m, "BM3_", 4);
    for (i = 4; i < 101; ++i) {
        unsigned char v = (unsigned char)((i * 37 + 11) & 0xFF);
        m[i] = v == 0x23 ? 0x24 : v;
    }
    m[101] = '#';
    memcpy(m + 102, tag, strlen(tag));
}

static const char *rom_of(const unsigned char *m, size_t size) {
    const char *why;
    const bm3_profile *p = bm3_marker_profile(m, size, &why);
    return p ? p->rom : NULL;
}

int main(void) {
    unsigned char m[BM3_MARKER_SIZE], bad[BM3_MARKER_SIZE], r[64], prefix[8];
    const bm3_profile *p;
    size_t i, n;

    /* S58 hash layout */
    hash_marker(m, "F4C9L8R02GGGJM_G87_RWD_USA_KOR_ULEV246_");
    CHECK(rom_of(m, sizeof m) && !strcmp(rom_of(m, sizeof m), "F4C9L8R02"));
    hash_marker(m, "ZZZZZZZZZGGGJM_G87_RWD_USA_");
    CHECK(!rom_of(m, sizeof m));
    hash_marker(m, "F4C9L8R02");
    CHECK(!rom_of(m, sizeof m));
    hash_marker(m, "F4C9L8R02GGGJM_G87_RWD_USA_");
    m[50] = '#';
    CHECK(!rom_of(m, sizeof m));

    /* Captured binary layout */
    binary_marker(m, "R4C9H5V02OKLX0");
    {
        const char *why;
        p = bm3_marker_profile(m, sizeof m, &why);
    }
    CHECK(p && !strcmp(p->rom, "R4C9H5V02") && p->map_address == 0x60038006u && p->write_service == 0x3E);
    memcpy(bad, m, sizeof bad); bad[0] = 0; CHECK(!rom_of(bad, sizeof bad));            /* damaged prefix */
    memcpy(bad, m, sizeof bad); bad[68] = 0; CHECK(!rom_of(bad, sizeof bad));           /* damaged delimiter */
    memcpy(bad, m, sizeof bad);
    bad[4 + 64 + 3 + 30 + 1 + 14] = '_';
    CHECK(!rom_of(bad, sizeof bad));                                                     /* no terminator */
    binary_marker(bad, "UNKNOWN123OKLX0"); CHECK(!rom_of(bad, sizeof bad));
    binary_marker(bad, "R4C9H5V02"); CHECK(!rom_of(bad, sizeof bad));                   /* no build characters */
    binary_marker(bad, "R4C9H5V02OKLX0EXTRA"); CHECK(!rom_of(bad, sizeof bad));
    CHECK(!rom_of(m, sizeof m - 1));                                                     /* short */
    memset(bad, 0, sizeof bad); CHECK(!rom_of(bad, sizeof bad));                        /* empty */

    /* F-series MEVD17 layout */
    mevd17_marker(m, "EPST:R1C9J8B3B", "R1C900000CEKXA");
    p = bm3_marker_profile(m, sizeof m, &(const char *){0});
    CHECK(p && !strcmp(p->rom, "R1C900000") && p->map_address == 0x4005E006u && p->write_service == 0x3E);
    mevd17_marker(bad, "EPST:R1C9J8B3B", "UNKNOWN12CEKXA"); CHECK(!rom_of(bad, sizeof bad));
    mevd17_marker(bad, "EPST:R1C9J8B3B", "R1C900000"); CHECK(!rom_of(bad, sizeof bad)); /* no build characters */
    mevd17_marker(bad, "R1C900000CEKXA", "EPST:R1C9J8B3B"); CHECK(!rom_of(bad, sizeof bad)); /* the ROM isn't last */
    memcpy(bad, m, sizeof bad); bad[2] = 'X'; CHECK(!rom_of(bad, sizeof bad));          /* damaged #DST */
    memcpy(bad, m, sizeof bad); bad[72] = 0; CHECK(!rom_of(bad, sizeof bad));           /* damaged delimiter */

    /* Every profile, in the legacy and binary layouts */
    CHECK(BM3_PROFILE_COUNT == 150);
    for (i = 0; i < BM3_PROFILE_COUNT; ++i) {
        char token[32];
        const char *rom = bm3_profiles[i].rom;
        memset(m, 0, sizeof m);
        snprintf((char *)m, sizeof m, "BM3_R#%s#", rom);
        if (!rom_of(m, sizeof m) || strcmp(rom_of(m, sizeof m), rom)) {
            fprintf(stderr, "legacy marker for %s\n", rom);
            ++failures;
        }
        snprintf(token, sizeof token, "%sOKLX0", rom);
        binary_marker(m, token);
        if (!rom_of(m, sizeof m) || strcmp(rom_of(m, sizeof m), rom)) {
            fprintf(stderr, "binary marker for %s\n", rom);
            ++failures;
        }
        CHECK(bm3_profiles[i].write_service == 0x3D || bm3_profiles[i].write_service == 0x3E);
    }

    /* The user's ROM */
    p = bm3_profile_for("R1C900000");
    CHECK(p && p->map_address == 0x4005E006u && p->write_service == 0x3E);

    /* Requests and reply prefixes, as upstream asserts them */
    {
        static const unsigned char clear[] = {0x2C, 3, 0xF3, 1};
        static const unsigned char define[] = {0x2C, 2, 0xF3, 1, 0x24, 0x80, 0x7F, 0xFE, 0x50, 0, 255};
        static const unsigned char read_did[] = {0x22, 0xF3, 1};
        static const unsigned char read_slot[] = {0x23, 0x14, 0x60, 3, 0x80, 6, 1};
        static const unsigned char direct[] = {0x23, 0x14, 0x80, 0x7F, 0xFE, 0x50, 255};
        static const unsigned char write3e[] = {0x3E, 0x14, 0x40, 0x05, 0xE0, 0x06, 2, 3, 1};
        static const unsigned char slot_data[] = {3, 1};
        n = bm3_clear_dynamic_did(r); CHECK(n == sizeof clear && !memcmp(r, clear, n));
        CHECK(bm3_reply_prefix(r, prefix) == 4 && !memcmp(prefix, "\x6C\x03\xF3\x01", 4));
        n = bm3_define_marker_did(r, 0x807FFE50u); CHECK(n == sizeof define && !memcmp(r, define, n));
        CHECK(bm3_reply_prefix(r, prefix) == 4 && !memcmp(prefix, "\x6C\x02\xF3\x01", 4));
        n = bm3_read_marker_did(r); CHECK(n == sizeof read_did && !memcmp(r, read_did, n));
        CHECK(bm3_reply_prefix(r, prefix) == 3 && !memcmp(prefix, "\x62\xF3\x01", 3));
        n = bm3_read_memory(r, 0x60038006u, 1); CHECK(n == sizeof read_slot && !memcmp(r, read_slot, n));
        CHECK(bm3_reply_prefix(r, prefix) == 1 && prefix[0] == 0x63);
        n = bm3_read_memory(r, 0x807FFE50u, 255); CHECK(n == sizeof direct && !memcmp(r, direct, n));
        n = bm3_write_memory(r, sizeof r, p, p->map_address, slot_data, 2);
        CHECK(n == sizeof write3e && !memcmp(r, write3e, n));
        CHECK(bm3_reply_prefix(r, prefix) == 2 && !memcmp(prefix, "\x7E\x3D", 2));
        {
            bm3_profile d = *p;
            d.write_service = 0x3D;
            n = bm3_write_memory(r, sizeof r, &d, d.map_address, slot_data, 2);
            CHECK(n == 9 && r[0] == 0x3D && bm3_reply_prefix(r, prefix) == 1 && prefix[0] == 0x7D);
            d.write_service = 0x27;
            CHECK(bm3_write_memory(r, sizeof r, &d, d.map_address, slot_data, 2) == 0);
        }
        CHECK(bm3_write_memory(r, 8, p, p->map_address, slot_data, 2) == 0); /* does not fit */
        CHECK(bm3_slot_reply((const unsigned char *)"\x63\x02", 2) == 2);
        CHECK(bm3_slot_reply((const unsigned char *)"\x63\x04", 2) == -1);
        CHECK(bm3_slot_reply((const unsigned char *)"\x63\x01\x00", 3) == -1);
    }

    /* Active Sound and burbles, as bm3_protocol.js sends them (setAsd, setBurbles) */
    {
        unsigned char table[BM3_BURBLE_TABLE], expected[BM3_BURBLE_TABLE];
        n = bm3_sound_request(r, 1); CHECK(n == 4 && !memcmp(r, "\x2E\x5F\xFD\x00", 4));
        CHECK(bm3_reply_prefix(r, prefix) == 3 && !memcmp(prefix, "\x6E\x5F\xFD", 3));
        n = bm3_sound_request(r, 0); CHECK(n == 4 && !memcmp(r, "\x2E\x5F\xFD\x01", 4));

        /* factor 2: aggression 4, 8 and 12 are -8, -16 and -24; off is 0 */
        memset(expected, 0, sizeof expected);
        CHECK(bm3_burble_table(p, 0, table) && !memcmp(table, expected, sizeof table));
        memset(expected, 0xF8, sizeof expected);
        CHECK(bm3_burble_table(p, 1, table) && !memcmp(table, expected, sizeof table));
        memset(expected, 0xF0, sizeof expected);
        CHECK(bm3_burble_table(p, 2, table) && !memcmp(table, expected, sizeof table));
        memset(expected, 0xE8, sizeof expected);
        CHECK(bm3_burble_table(p, 3, table) && !memcmp(table, expected, sizeof table));
        CHECK(!bm3_burble_table(p, 4, table));
        {
            bm3_profile d = *p;
            d.factor = 11; /* 12 * 11 > 127: the byte would wrap */
            CHECK(!bm3_burble_table(&d, 1, table));
            d.factor = 0;
            CHECK(!bm3_burble_table(&d, 0, table));
        }
        for (i = 0; i < BM3_PROFILE_COUNT; ++i) CHECK(bm3_burble_table(&bm3_profiles[i], BM3_BURBLE_PRESETS, table));
    }
    if (failures) return 1;
    puts("BM3: marker layouts, all 150 profiles, requests, replies and burble tables passed.");
    return 0;
}
