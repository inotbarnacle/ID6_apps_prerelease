/* Generated from the project's ISTA SGBD research (see docs/F150.md). Key: ECU address + the
 * 24-bit index a car returns to 22 F150, read from the group's own ECU address. F- and G-series
 * only; sorted by ecu, index. Not read on a car except where another file says so. */
#ifndef F150_TABLE_H
#define F150_TABLE_H

struct f150_entry { unsigned char ecu; unsigned index; const char *sgbd; };
static const struct f150_entry f150_table[] = {
    {0x12, 0x0F1090, "MSD85L6"}, /* DME F01 */
    {0x12, 0x0F10A0, "D73N57A0"}, /* DME F01 */
    {0x12, 0x0F1370, "MSD87"}, /* DME F01 */
    {0x12, 0x0F1380, "MSD87_R0"}, /* DME F01 */
    {0x12, 0x0F13E0, "MSV90"}, /* DME F01 */
    {0x12, 0x0F13F0, "MEVD172"}, /* DME F01 */
    {0x12, 0x0F1740, "D72N47A0"}, /* DME F25 */
    {0x12, 0x0F1780, "MSD85YL6"}, /* DME F04 */
    {0x12, 0x0F17B0, "D73N57E0"}, /* DME F10 */
    {0x12, 0x0F17B4, "D73N57E0"}, /* DME F10 */
    {0x12, 0x0F17B8, "D73N57E0"}, /* DME F10 */
    {0x12, 0x0F17BC, "D73N57E0"}, /* DME F10 */
    {0x12, 0x0F17F0, "MEVD1725"}, /* DME F20 */
    {0x12, 0x0F1810, "MEVD1724"}, /* DME F25 */
    {0x12, 0x0F18A0, "D72N47A0"}, /* DME F25 */
    {0x12, 0x0F1910, "MEVD172Y"}, /* DME F10 */
    {0x12, 0x0F1920, "D72N47A0"}, /* DME F10 */
    {0x12, 0x0F1A10, "S63TU_R0"}, /* DME F10 */
    {0x12, 0x0F1A20, "N63TU_R0"}, /* DME F01 */
    {0x12, 0x0F1D20, "DME_BX8"}, /* DME F56 */
    {0x12, 0x0F1D22, "DME_BX8"}, /* DME F56 */
    {0x12, 0x0F1D26, "DME_BX8"}, /* DME F48 */
    {0x12, 0x0F1EA0, "S55"}, /* DME F80 */
    {0x12, 0x0F1EF0, "D73N57E0"}, /* DME F15 */
    {0x12, 0x0F1F70, "D75BX7A0"}, /* DME F30 */
    {0x12, 0x0F1F80, "D75BX7A0"}, /* DME F30 */
    {0x12, 0x0F1F90, "D70BX7A0"}, /* DME F30 */
    {0x12, 0x0F1FA0, "D70BX7A0"}, /* DME F30 */
    {0x12, 0x0F1FE8, "D83BX7C0"}, /* DME G11 */
    {0x12, 0x0F1FF0, "D83BX7C0"}, /* DME G30 */
    {0x12, 0x0F1FF4, "D83BX7C0"}, /* DME F34 */
    {0x12, 0x0F1FF8, "D83BX7C0"}, /* DME F60 */
    {0x12, 0x0F2220, "N55_ALP"}, /* DME F30 */
    {0x12, 0x0F2422, "DME8FF_R"}, /* DME G20 */
    {0x12, 0x0F2424, "DME8FF_R"}, /* DME F36 */
    {0x12, 0x0F2426, "DME8FF_R"}, /* DME G11 */
    {0x12, 0x0F2427, "DME8FF_R"}, /* DME F36 */
    {0x12, 0x0F2428, "DME8FF_R"}, /* DME F36 */
    {0x12, 0x0F2429, "DME8FF_R"}, /* DME F36 */
    {0x12, 0x0F242A, "DME8FF_R"}, /* DME G11 */
    {0x12, 0x0F242C, "DME8FF_R"}, /* DME G11 */
    {0x12, 0x0F242E, "DME8FF_R"}, /* DME G11 */
    {0x12, 0x0F2460, "DME8FF_R"}, /* DME F90 */
    {0x12, 0x0F25A0, "DME8FF_R"}, /* DME F56 */
    {0x12, 0x0F25A2, "DME8FF_R"}, /* DME F56 */
    {0x12, 0x0F2670, "D83BX7C0"}, /* DME F55 */
    {0x12, 0x0F2674, "D83BX7C0"}, /* DME F40 */
    {0x12, 0x0F2678, "D83BX7C0"}, /* DME G30 */
    {0x12, 0x0F267C, "D83BX7C0"}, /* DME G14 */
    {0x12, 0x0F26B4, "DME8FF_R"}, /* DME G05 */
    {0x12, 0x0F26B8, "DME8FF_R"}, /* DME G14 */
    {0x12, 0x0F26E0, "DME8FF_R"}, /* DME G30 */
    {0x12, 0x0F26E4, "DME8FF_R"}, /* DME F40 */
    {0x12, 0x0F26E8, "DME8FF_R"}, /* DME F97 */
    {0x12, 0x0F2930, "DME8FF_R"}, /* DME G11 */
    {0x12, 0x0F2C60, "FCCU"}, /* DME G05 */
    {0x12, 0x0F2E00, "DME98_R"}, /* DME G09 */
    {0x12, 0x0F3690, "DME9X2"}, /* DME G65 */
    {0x12, 0x0F36A0, "D95BX7A0"}, /* DME G70 */
    {0x12, 0x0F36E0, "H2CU_03"}, /* DME G65FCEV */
    {0x12, 0x0F3880, "DME982_R"}, /* DME G90 */
    {0x12, 0x0F3AC0, "DME96M_R"}, /* DME G84 */
    {0x12, 0x0F3B30, "DME9X3"}, /* DME G78 */
    {0x13, 0x0F1380, "MSD87_L0"}, /* DME F01 */
    {0x13, 0x0F1A10, "S63TU_L0"}, /* DME F10 */
    {0x13, 0x0F1A20, "N63TU_L0"}, /* DME F01 */
    {0x13, 0x0F242C, "DME8FF_L"}, /* DME G11 */
    {0x13, 0x0F242E, "DME8FF_L"}, /* DME G11 */
    {0x13, 0x0F2460, "DME8FF_L"}, /* DME F90 */
    {0x13, 0x0F26B8, "DME8FF_L"}, /* DME G14 */
    {0x13, 0x0F2E00, "DME98_L"}, /* DME G09 */
    {0x13, 0x0F3880, "DME982_L"}, /* DME G90 */
    {0x18, 0x0F1270, "GS100A"}, /* EGS F01 */
    {0x18, 0x0F14B0, "GSB231"}, /* EGS F07 */
    {0x18, 0x0F14B8, "GKEB23TU"}, /* EGS F15 */
    {0x18, 0x0F1870, "DKG_10"}, /* EGS F10 */
    {0x18, 0x0F1890, "GKEHY23"}, /* EGS F10 */
    {0x18, 0x0F1C80, "GSAW01"}, /* EGS F56 */
    {0x18, 0x0F1F20, "GKEG3HY"}, /* EGS F18 */
    {0x18, 0x0F21D0, "GSZFB1"}, /* EGS G11 */
    {0x18, 0x0F21E0, "GSZFBHY1"}, /* EGS G11 */
    {0x18, 0x0F2380, "GSGE01QT"}, /* EGS F56 */
    {0x18, 0x0F2388, "GSGE01QW"}, /* EGS F56 */
    {0x18, 0x0F23A0, "GSAW02FH"}, /* EGS F56 */
    {0x18, 0x0F23A8, "GSAW02FP"}, /* EGS F56 */
    {0x18, 0x0F25D0, "GSZFBCE"}, /* EGS G05 */
    {0x18, 0x0F2D60, "GSZF04GA"}, /* EGS G14 */
    {0x18, 0x0F2D70, "GSZF04GD"}, /* EGS G70 */
    {0x18, 0x0F2D80, "GSZF04GF"}, /* EGS G70 */
    {0x18, 0x0F3530, "GSZF04GJ"}, /* EGS G65 */
    {0x18, 0x0F3540, "GSZF04GN"}, /* EGS G65 */
    {0x19, 0x0F1480, "LMV_01"}, /* LMV F01 */
    {0x19, 0x0F2180, "LMV_G11"}, /* LMV G11 */
    {0x19, 0x0F2190, "LMV_F45"}, /* LMV F45 */
    {0x19, 0x0F29E0, "LMV_G70"}, /* LMV G70 */
    {0x19, 0x0F35C0, "LMV_G65"}, /* LMV G65 */
    {0x29, 0x0F1060, "DSC_01"}, /* DSC F01 */
    {0x29, 0x0F1620, "DSC_25"}, /* DSC F25 */
    {0x29, 0x0F1860, "DSC_25"}, /* DSC F20 */
    {0x29, 0x0F18F0, "DSC_25"}, /* DSC F10 */
    {0x29, 0x0F1BD0, "DSC_10"}, /* DSC F10 */
    {0x29, 0x0F1C50, "DSC_I1"}, /* DSC F56 */
    {0x29, 0x0F1F30, "DSC_25"}, /* DSC F80 */
    {0x29, 0x0F2100, "DSC_G11"}, /* DSC G11 */
    {0x29, 0x0F2500, "IB_G05"}, /* DSC G05 */
    {0x29, 0x0F2510, "DSC_G20"}, /* DSC G20 */
    {0x29, 0x0F2850, "DSC_G30N"}, /* DSC G30 */
    {0x29, 0x0F2CE0, "IB_G2X"}, /* DSC G26BEV */
    {0x29, 0x0F2E70, "IB_G70"}, /* DSC G70 */
    {0x29, 0x0F34C0, "IB_CT03X"}, /* DSC G65 */
    {0x39, 0x0F2520, "VIP_G05"}, /* DSC G05 */
    {0x39, 0x0F2530, "VIP_G05"}, /* DSC G20 */
    {0x39, 0x0F2860, "VIP_G30N"}, /* DSC G30 */
};

/* NULL if this (ecu, index) pair is not in the table -- an unknown or newer SGBD. */
static inline const char *f150_sgbd(unsigned char ecu, unsigned index) {
    unsigned i;
    for (i = 0; i < sizeof f150_table / sizeof f150_table[0]; ++i)
        if (f150_table[i].ecu == ecu && f150_table[i].index == index) return f150_table[i].sgbd;
    return 0;
}

#endif
