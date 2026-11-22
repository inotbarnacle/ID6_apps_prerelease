#include "f150_table.h"
#include "dme_signal_table.h"
#include "egs_signal_table.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const struct dme_sgbd_row *row;
    const struct egs_sgbd_row *egs;
    assert(!strcmp(f150_sgbd(0x12, 0x0F17F0), "MEVD1725"));     /* F20 N13 */
    assert(!strcmp(f150_sgbd(0x29, 0x0F1860), "DSC_25"));       /* F20 DSC */
    assert(!strcmp(f150_sgbd(0x18, 0x0F25D0), "GSZFBCE"));      /* G05 EGS */
    assert(!f150_sgbd(0x12, 0));                                /* unknown index */
    assert(!f150_sgbd(0x99, 0x0F17F0));                         /* unknown ECU */

    row = dme_signal_row("D73N57A0"); /* diesel DDE: ambient, boost, intake air, rail, coolant; no oil/AFR/timing */
    assert(row && row->slot[0].did == 0x4CF0 && row->slot[7].did == 0x461B && !row->slot[2].present &&
           !row->slot[3].present && !row->slot[6].present);
    row = dme_signal_row("d83bx7c0"); /* DDE8: intake air is 47ED, 2 bytes, not 520F */
    assert(row && row->slot[4].did == 0x47ED && row->slot[4].width == 2);
    row = dme_signal_row("dme9x2"); /* case-insensitive, lowercase table key */
    assert(row && !strcmp(row->sgbd, "dme9x2"));
    assert(row->slot[3].present == 0);      /* timing: no verified reading on this SGBD */
    assert(row->slot[7].present == 1 && row->slot[7].did == 0x4C97); /* coolant override */
    assert(row->slot[4].width == 1);        /* intake air is one byte on dme9x2/dme982 (SGBD RTANS) */
    assert(dme_signal_row("dme982_r")->slot[3].did == 0x59FA && dme_signal_row("dme98_r")->slot[6].did == 0xEF58);
    assert(egs_signal_row("gsaw02fh")->trans_temp.did == 0x403D);
    assert(row->slot[8].present && row->slot[8].did == 0xEC3F && row->slot[8].width == 2); /* intercooler (NT) coolant */
    assert(!dme_signal_row("dme8ff_r")->slot[8].present); /* dme8ff_r defines no NT-circuit coolant DID (only 5018 charge air and 55DC pump electronics): a table gap, not a hardware fact */
    assert(!dme_signal_row("not-a-real-sgbd"));
    assert(!dme_signal_row(0));

    /* Car-verified (upstream issue #117): S55 answers coolant as one byte, not two. */
    row = dme_signal_row("s55");
    assert(row && row->slot[7].present && row->slot[7].width == 1 && row->slot[7].did == 0x4300);

    /* gszfbce: EGS transmission temperature, confirmed independently against idrive7-apps'
       car-verified zf8hp catalog. */
    egs = egs_signal_row("GSZFBCE");
    assert(egs && egs->trans_temp.present && egs->trans_temp.did == 0x6000 &&
           egs->trans_temp.width == 1 && !egs->trans_temp.is_signed);
    assert(!egs_signal_row("not-a-real-sgbd"));

    /* The gen-4 8HP family repeats gszfbce's DID and formula by name in each SGBD. */
    egs = egs_signal_row("gszf04gn");
    assert(egs && egs->trans_temp.did == 0x6000 && egs->trans_temp.add == -40.0);

    /* dkg_10 (F10-era DKG dual-clutch) and gsge01qt (F56 MINI DCT): new, unconfirmed on a car. */
    egs = egs_signal_row("dkg_10");
    assert(egs && egs->trans_temp.present && egs->trans_temp.did == 0x6060 &&
           egs->trans_temp.width == 2 && egs->trans_temp.is_signed);
    egs = egs_signal_row("GSGE01QT");
    assert(egs && egs->trans_temp.present && egs->trans_temp.did == 0x40A0 &&
           egs->trans_temp.div == 4.0);

    puts("F150: ECU+index lookup and per-SGBD signal overrides passed.");
    return 0;
}
