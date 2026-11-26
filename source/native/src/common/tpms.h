/* Provisional RDC (tyre pressure) request for ShiftDeck.
 *
 * Neither the RDC ECU address, the DID, nor the reply layout has been captured
 * from a car yet: TPMS_ECU/TPMS_DID below are placeholders, and tpms_decode
 * assumes one DID returning four big-endian uint16 pressures in hPa
 * (FL, FR, RL, RR). Do not enable this on a car until a parked capture with
 * probe_signals.py confirms the request and the conversion against the
 * cluster/iDrive reading. See docs/SIGNALS.md (TPMS, provisional).
 */
#ifndef ID6_TPMS_H
#define ID6_TPMS_H

#include <stddef.h>

enum { TPMS_ECU = 0x00, TPMS_DID = 0x0000, TPMS_POLL_MS = 10000 };

/* Four big-endian uint16 pressures in hPa. 0 is a flat tyre and stays valid;
 * 0xFFFF (invalid sentinel) and values above 6000 fail. Values publish only
 * after all four corners validate, so a failure never leaves a partial mix
 * behind the last fresh timestamp. Returns 1 decoded. */
static int tpms_decode(const unsigned char *data, size_t len, int hpa[4]) {
    unsigned i;
    int fresh[4];
    if (!data || !hpa || len != 8) return 0;
    for (i = 0; i < 4; ++i) {
        unsigned raw = ((unsigned)data[2*i] << 8) | data[2*i+1];
        if (raw == 0xffff || raw > 6000) return 0;
        fresh[i] = (int)raw;
    }
    hpa[0]=fresh[0]; hpa[1]=fresh[1]; hpa[2]=fresh[2]; hpa[3]=fresh[3];
    return 1;
}

#endif
