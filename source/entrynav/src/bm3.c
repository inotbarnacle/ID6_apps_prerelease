/* See bm3.h. Ported from cauliflowermossgrape's bm3_protocol.js (markerDataRom, binaryMarkerTag, hashMarkerTag, Bm3Protocol). */
#include "bm3.h"
#include "bm3_profiles.h"
#include <string.h>

const bm3_profile *bm3_profile_for(const char *rom) {
    size_t i;
    for (i = 0; i < BM3_PROFILE_COUNT; ++i)
        if (!strcmp(bm3_profiles[i].rom, rom)) return &bm3_profiles[i];
    return NULL;
}

static int upper_alnum(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

static int contains(const unsigned char *data, size_t size, const char *needle) {
    size_t n = strlen(needle), i;
    for (i = 0; i + n <= size; ++i)
        if (!memcmp(data + i, needle, n)) return 1;
    return 0;
}

/* binaryMarkerTag: BM3_, 64 binary bytes, ~|~, then underscores and #tags up to a NUL. The ROM tag is the last one,
 * [A-Z0-9] only, and goes to tag. MG1 starts at BM3_ with a single tag (upstream's layout):
 *     BM3_ <64> ~|~ ___ #R4C9H5V02OKLX0 \0
 * The F-series MEVD17 puts #DST in front, and the stock ROM's tag before the bootmod3 one (an F20 M140i, 2026-11-15):
 *     #DST BM3_ <64> ~|~ ____ #EPST:R1C9J8B3B ______ #R1C900000CEKXA \0 */
static int binary_marker_tag(const unsigned char *m, char *tag, size_t cap) {
    size_t base, i, start, n;
    if (!memcmp(m, "BM3_", 4)) base = 0;
    else if (!memcmp(m, "#DSTBM3_", 8)) base = 4;
    else return 0;
    if (memcmp(m + base + 68, "~|~", 3)) return 0;
    i = base + 71;
    if (m[i] != '_') return 0;
    for (;;) {
        while (i < BM3_MARKER_SIZE && m[i] == '_') ++i;
        if (i >= BM3_MARKER_SIZE || m[i] != '#') return 0;
        start = ++i;
        while (i < BM3_MARKER_SIZE && m[i] >= 0x20 && m[i] < 0x7f && m[i] != '_' && m[i] != '#') ++i;
        if (i >= BM3_MARKER_SIZE || i == start) return 0;
        if (m[i] == 0) break; /* the last tag: the ROM's */
        if (m[i] != '_') return 0;
    }
    n = i - start;
    if (n >= cap) return 0;
    for (i = start; i < start + n; ++i)
        if (!upper_alnum(m[i])) return 0;
    memcpy(tag, m + start, n);
    tag[n] = 0;
    return 1;
}

/* hashMarkerTag: BM3_, the only # at byte 101, then ROM (9) + build (5) + _ + more of [A-Z0-9_], ended by NUL.
 * Returns the first 9 characters in rom. */
static int hash_marker_rom(const unsigned char *m, char *rom) {
    size_t i, nul, length;
    if (memcmp(m, "BM3_", 4)) return 0;
    for (i = 0; i < BM3_MARKER_SIZE; ++i)
        if (m[i] == '#' && i != 101) return 0;
    if (m[101] != '#') return 0;
    for (nul = 102; nul < BM3_MARKER_SIZE && m[nul]; ++nul) {
    }
    if (nul >= BM3_MARKER_SIZE) return 0;
    length = nul - 102;
    if (length < 15 || m[102 + 14] != '_') return 0;
    for (i = 102; i < nul; ++i)
        if (!upper_alnum(m[i]) && m[i] != '_') return 0;
    for (i = 102; i < 102 + 14; ++i)
        if (m[i] == '_') return 0;
    memcpy(rom, m + 102, 9);
    rom[9] = 0;
    return 1;
}

const bm3_profile *bm3_marker_profile(const unsigned char *m, size_t size, const char **why) {
    const bm3_profile *found = NULL;
    char tag[BM3_MARKER_SIZE], rom[10];
    size_t i, matches = 0;
    *why = NULL;
    if (size != BM3_MARKER_SIZE) { *why = "marker length"; return NULL; }
    if (binary_marker_tag(m, tag, sizeof tag)) {
        /* The captured MG1 token appends five build characters: R4C9H5V02OKLX0. */
        size_t tag_length = strlen(tag);
        for (i = 0; i < BM3_PROFILE_COUNT; ++i) {
            size_t key = strlen(bm3_profiles[i].rom), j;
            int ok = tag_length == key + 5 && !strncmp(tag, bm3_profiles[i].rom, key);
            for (j = key; ok && j < tag_length; ++j) ok = upper_alnum((unsigned char)tag[j]);
            if (ok) { found = &bm3_profiles[i]; ++matches; }
        }
        if (matches != 1) { *why = "ROM not uniquely matched in the binary marker"; return NULL; }
        return found;
    }
    if (hash_marker_rom(m, rom)) {
        found = bm3_profile_for(rom);
        if (!found) *why = "ROM has no profile";
        return found;
    }
    if (!contains(m, size, "BM3_R")) { *why = "no bootmod3 CustomROM marker"; return NULL; }
    for (i = 0; i < BM3_PROFILE_COUNT; ++i) {
        char needle[16] = "#";
        strcat(needle, bm3_profiles[i].rom);
        if (contains(m, size, needle)) { found = &bm3_profiles[i]; ++matches; }
    }
    if (matches != 1) { *why = "ROM not uniquely matched in the marker"; return NULL; }
    return found;
}

static size_t put32(unsigned char *out, uint32_t value) {
    out[0] = (unsigned char)(value >> 24);
    out[1] = (unsigned char)(value >> 16);
    out[2] = (unsigned char)(value >> 8);
    out[3] = (unsigned char)value;
    return 4;
}

size_t bm3_clear_dynamic_did(unsigned char *out) {
    static const unsigned char r[] = {0x2C, 0x03, 0xF3, 0x01};
    memcpy(out, r, sizeof r);
    return sizeof r;
}

size_t bm3_define_marker_did(unsigned char *out, uint32_t address) {
    static const unsigned char head[] = {0x2C, 0x02, 0xF3, 0x01, 0x24};
    memcpy(out, head, sizeof head);
    put32(out + 5, address);
    out[9] = 0;
    out[10] = BM3_MARKER_SIZE;
    return 11;
}

size_t bm3_read_marker_did(unsigned char *out) {
    out[0] = 0x22;
    out[1] = 0xF3;
    out[2] = 0x01;
    return 3;
}

size_t bm3_read_memory(unsigned char *out, uint32_t address, unsigned length) {
    out[0] = 0x23;
    out[1] = 0x14;
    put32(out + 2, address);
    out[6] = (unsigned char)length;
    return 7;
}

size_t bm3_write_memory(unsigned char *out, size_t cap, const bm3_profile *profile, uint32_t address,
                        const unsigned char *data, unsigned length) {
    if (!profile || (profile->write_service != 0x3D && profile->write_service != 0x3E)) return 0;
    if (!length || length > 0xFF || 7 + (size_t)length > cap) return 0;
    out[0] = (unsigned char)profile->write_service;
    out[1] = 0x14;
    put32(out + 2, address);
    out[6] = (unsigned char)length;
    memcpy(out + 7, data, length);
    return 7 + length;
}

size_t bm3_sound_request(unsigned char *out, int on) {
    out[0] = 0x2E;
    out[1] = 0x5F;
    out[2] = 0xFD;
    out[3] = on ? 0x00 : 0x01;
    return 4;
}

int bm3_burble_table(const bm3_profile *profile, unsigned preset, unsigned char *table) {
    unsigned degrees;
    if (!profile || preset > BM3_BURBLE_PRESETS || !profile->factor ||
        BM3_BURBLE_PRESETS * BM3_BURBLE_STEP * profile->factor > 127)
        return 0;
    degrees = preset * BM3_BURBLE_STEP * profile->factor;
    memset(table, (unsigned char)(0x100u - degrees), BM3_BURBLE_TABLE); /* the two's complement byte of -degrees */
    return 1;
}

size_t bm3_reply_prefix(const unsigned char *request, unsigned char *prefix) {
    prefix[0] = (unsigned char)(request[0] + 0x40);
    switch (request[0]) {
    case 0x2C: memcpy(prefix + 1, request + 1, 3); return 4; /* 6C 0X F3 01 */
    case 0x22: memcpy(prefix + 1, request + 1, 2); return 3; /* 62 F3 01 */
    case 0x2E: memcpy(prefix + 1, request + 1, 2); return 3; /* 6E 5F FD */
    case 0x3E: prefix[1] = 0x3D; return 2;                   /* 7E 3D: bootmod3 answers its write this way */
    default: return 1;                                       /* 63, 7D */
    }
}

int bm3_slot_reply(const unsigned char *reply, size_t size) {
    if (size != 2 || reply[0] != 0x63 || reply[1] >= BM3_SLOTS) return -1;
    return reply[1];
}
