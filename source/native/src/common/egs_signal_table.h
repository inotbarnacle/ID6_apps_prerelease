/* Generated from the project's ISTA SGBD research (see docs/F150.md). One row per EGS SGBD this
 * repo has a transmission-temperature reading for, read from the group's own ECU (0x18) rather
 * than the dme8ff_r-generation default (DA12, 2 bytes, raw-48). A NULL/absent row keeps DA12
 * untouched, same as an unlisted DME SGBD in dme_signal_table.h.
 *
 * Confidence varies by row -- none of this has been read on a car by this project, and only the
 * gszfbce row has outside confirmation:
 *
 * - gszfbce (8HP TU 2018, G05/G22-era): DID 0x6000, 1 byte, raw-40C. Independently confirmed: this
 *   SGBD's own FUMWELTTEXTE table (service 22;2C), and a car-verified read on a G22 by the sibling
 *   idrive7-apps project's zf8hp app (catalog.js: "_GETRIEBEOELTEMPERATUR", 1,1,false,false,1,-40,
 *   "C").
 * - gszf04ga/gd/gf/gj/gn (8HP generation 4, serves V8 N63/S63 cars): same DID 0x6000, same 1 byte
 *   raw-40C formula, by name ("GETRIEBEOELTEMPERATUR"/"_GETRIEBEOELTEMPERATUR") in all five SGBDs'
 *   own tables. Not cross-checked outside this project, but the identical convention repeating
 *   across six independently-dumped SGBD files is itself corroborating.
 * - dkg_10 (the F10-era DKG dual-clutch gearbox): DID 0x6060, "Temperatur Sumpf-Öl" (sump oil
 *   temperature), 2 bytes signed, raw (no scale). A new finding from this SGBD alone, with no
 *   outside or car confirmation; dkg_10 also defines a TCU (electronics) temperature at 0x6002
 *   and modeled per-clutch temperatures at 0x6079/0x607A/0x6084/0x6085, any of which could turn
 *   out to be the better match for "transmission temperature" once read on a car.
 * - gsge01qt/qw (F56 MINI 7-speed E-shift DCT): DID 0x40A0, "temp_sens_sig", 2 bytes signed,
 *   raw/4. Also new and unconfirmed; this SGBD's own tcu_temp (0x4205) and per-clutch friction/
 *   protection temperatures (0x4216-0x4219) are the same kind of alternative candidates. */
#ifndef EGS_SIGNAL_TABLE_H
#define EGS_SIGNAL_TABLE_H
#include <strings.h>

struct egs_signal_slot { unsigned char present, width, is_signed; unsigned did; double mul, div, add, lo, hi; };
struct egs_sgbd_row { const char *sgbd; struct egs_signal_slot trans_temp; };

static const struct egs_sgbd_row egs_signal_table[] = {
    {"gszfbce",  {1,1,0,0x6000,1.0,1.0,-40.0,-40,150}},
    {"gszf04ga", {1,1,0,0x6000,1.0,1.0,-40.0,-40,150}},
    {"gszf04gd", {1,1,0,0x6000,1.0,1.0,-40.0,-40,150}},
    {"gszf04gf", {1,1,0,0x6000,1.0,1.0,-40.0,-40,150}},
    {"gszf04gj", {1,1,0,0x6000,1.0,1.0,-40.0,-40,150}},
    {"gszf04gn", {1,1,0,0x6000,1.0,1.0,-40.0,-40,150}},
    {"dkg_10",   {1,2,1,0x6060,1.0,1.0,0.0,-40,150}},
    {"gsge01qt", {1,2,1,0x40A0,1.0,4.0,0.0,-40,150}},
    {"gsge01qw", {1,2,1,0x40A0,1.0,4.0,0.0,-40,150}},
    {"gsaw01", {1,1,0,0x403D,1.0,1.0,-40.0,-40,150}},
    {"gsaw02fh", {1,1,0,0x403D,1.0,1.0,-40.0,-40,150}},
    {"gsaw02fp", {1,1,0,0x403D,1.0,1.0,-40.0,-40,150}},
    {"gkeb23tu", {1,1,0,0x0001,1.0,1.0,-40.0,-40,150}},
    {"gkeg3hy", {1,1,0,0x0001,1.0,1.0,-40.0,-40,150}},
    {"gsb231", {1,1,0,0x0001,1.0,1.0,-40.0,-40,150}},
};

/* Case-insensitive; NULL if this SGBD has no row. */
static inline const struct egs_sgbd_row *egs_signal_row(const char *sgbd) {
    unsigned i;
    if (!sgbd) return 0;
    for (i = 0; i < sizeof egs_signal_table / sizeof egs_signal_table[0]; ++i)
        if (!strcasecmp(sgbd, egs_signal_table[i].sgbd)) return &egs_signal_table[i];
    return 0;
}

#endif
