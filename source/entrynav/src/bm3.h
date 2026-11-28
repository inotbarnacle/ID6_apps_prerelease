/* bootmod3 CustomROM map slots on the DME, ported from cauliflowermossgrape's bm3_protocol.js (MIT,
 * third-party/cauliflowermossgrape-LICENSE), itself from BM3 Live by ezl0l.
 * This project is independent of and unaffiliated with bootmod3.
 *
 * Map switching is not a BMW job: bootmod3's code in the DME handles it. The app reads the CustomROM marker from DME
 * memory and goes on only when it names a ROM in bm3_profiles.h. The map slot and the burble table are then written to
 * the profile's addresses with bootmod3's memory-write command and read back. Active Sound is one request, as
 * bm3_protocol.js sends it. bm3_protocol.js's exhaust flap is left out: bootmod3 sets the flap in each map, and the DME
 * lets a tester's flap request go after about 20 s. Everything here is pure: building requests and checking replies.
 * The I/O is in collector.c (BM3_MAPS_ONLY). */
#ifndef BM3_H
#define BM3_H

#include <stddef.h>
#include <stdint.h>

enum {
    BM3_DME = 0x12,
    BM3_MARKER_SIZE = 0xFF,
    BM3_SLOTS = 4,
    BM3_BURBLE_TABLE = 24,  /* bytes in the timing table (KF_DZWMIN_SPORT_SOT) */
    BM3_BURBLE_PRESETS = 3, /* off, then aggression 4, 8 and 12 */
    BM3_BURBLE_STEP = 4
};

typedef struct {
    const char *rom;
    uint32_t map_address;    /* the active slot (0..3), then an activation flag */
    uint32_t burble_address; /* burbles on (1) or off (0) */
    uint32_t timing_address; /* the burble timing table, BM3_BURBLE_TABLE signed bytes */
    unsigned factor;         /* the table's units per degree of aggression */
    unsigned write_service;  /* 0x3D WriteMemoryByAddress, or 0x3E as bootmod3 uses it on some ROMs */
} bm3_profile;

/* The profile for a ROM, or NULL. */
const bm3_profile *bm3_profile_for(const char *rom);

/* The ROM key named in the marker's 255 bytes, or NULL with *why set. Three layouts, as upstream:
 * a binary block (BM3_, 64 bytes, ~|~, underscores, #ROM + 5 build characters, NUL),
 * the S58 one (BM3_, binary, the only # at byte 101, ROM (9) + build (5) + _ + chassis..., NUL),
 * and the older text one (BM3_R somewhere, #ROM). A ROM must match exactly one profile. */
const bm3_profile *bm3_marker_profile(const unsigned char *marker, size_t size, const char **why);

/* Requests, as UDS bytes for the DME. Each returns its length. */
size_t bm3_clear_dynamic_did(unsigned char *out);                     /* 2C 03 F3 01 */
size_t bm3_define_marker_did(unsigned char *out, uint32_t address);  /* 2C 02 F3 01 24 AAAAAAAA 00 FF */
size_t bm3_read_marker_did(unsigned char *out);                       /* 22 F3 01 */
size_t bm3_read_memory(unsigned char *out, uint32_t address, unsigned length); /* 23 14 AAAAAAAA LL */
/* Write: SID 14 AAAAAAAA LL data. 0 if the profile's service is unknown or the data doesn't fit. */
size_t bm3_write_memory(unsigned char *out, size_t cap, const bm3_profile *profile, uint32_t address,
                        const unsigned char *data, unsigned length);

/* Active Sound, DID 5FFD: 2E 5F FD 00 turns it on, 2E 5F FD 01 off. */
size_t bm3_sound_request(unsigned char *out, int on);
/* The burble timing table for preset 0..BM3_BURBLE_PRESETS: every byte -(preset * BM3_BURBLE_STEP * factor), as a
 * signed byte, so 0 for off. 0 if the preset or the profile's factor is out of range, 1 otherwise. */
int bm3_burble_table(const bm3_profile *profile, unsigned preset, unsigned char *table);

/* The positive reply's first bytes for a request built above (the SID + 0x40, then what the DME echoes). */
size_t bm3_reply_prefix(const unsigned char *request, unsigned char *prefix);

/* Parses a one-byte slot read (63 SS). Returns 0..3, or -1. */
int bm3_slot_reply(const unsigned char *reply, size_t size);

#endif
