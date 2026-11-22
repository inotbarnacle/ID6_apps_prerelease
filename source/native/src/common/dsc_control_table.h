/* Generated from the project's xDrive SGBD research (see docs/F150.md). ECU 0x29 (DSC/IB) rows
 * only; the VIP SGBDs at 0x39 use DB2F/DB44, a mechanism this worker's control() does not
 * implement, so they are not in this table (xdrive_control_worker.c's --status still reads them
 * directly for display). verified_write is true only for the (ecu, index) pairs the shipped code
 * already wrote to before this table existed -- AGENTS.md: an ECU write needs its own validated
 * control read before its scope grows, so adding a DSC here does not by itself enable writing to
 * it; that still needs a car-verified read for that specific SGBD. */
#ifndef DSC_CONTROL_TABLE_H
#define DSC_CONTROL_TABLE_H

struct dsc_control_row { unsigned index; const char *sgbd; const char *chassis; unsigned char verified_write; };
static const struct dsc_control_row dsc_control_table[] = {
    {0x0F1060, "DSC_01", "F01", 0},
    {0x0F1620, "DSC_25", "F25", 0},
    {0x0F1860, "DSC_25", "F20", 1},
    {0x0F18F0, "DSC_25", "F10", 0},
    {0x0F1F30, "DSC_25", "F80", 0},
    {0x0F1C50, "DSC_I1", "F56", 0},
    {0x0F1BD0, "DSC_10", "F10", 0},
    {0x0F2100, "DSC_G11", "G11", 0},
    {0x0F2CE0, "IB_G2X", "G26BEV", 0},
    {0x0F2510, "DSC_G20", "G20", 1},
    {0x0F2850, "DSC_G30N", "G30", 1},
};

/* NULL if this 22 F150 index on ECU 0x29 is not in the table. */
static inline const struct dsc_control_row *dsc_control_row(unsigned index) {
    unsigned i;
    for (i = 0; i < sizeof dsc_control_table / sizeof dsc_control_table[0]; ++i)
        if (dsc_control_table[i].index == index) return &dsc_control_table[i];
    return 0;
}

#endif
