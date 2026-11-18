// Bar ranges are the stored units from gauge_core.h. Each pair is chosen so the
// usual Supra reading is the middle of the bar. Fill past that middle blends
// from green through yellow to red.
static const unsigned kDialSteps = 32;
static const unsigned kBarFirst = 55000;
static const unsigned kHeatPalette = 16;
static const unsigned kMulticolor = 8;
static const char *const kColorNames[] = {"BMW Orange", "Blue", "Green", "White", "Purple", "Pink", "Cyan", "Red", "Multicolor"};
static const struct DialScale { double minimum, maximum; const char *name, *unit; } kDialScales[] = {
    /* 0 to 36 psi. 18 psi is the middle. */
    {0, 36, "Boost", "psi"},
    /* 60 F to 380 F. 220 F is the middle, and a cold 140 F still fills the bar. */
    {15.5555556, 193.3333333, "Oil temp", "\260F"},
    /* -20 to 30 degrees. 5 degrees is the middle. */
    {-20, 30, "Timing", "deg"},
    /* 30 F to 160 F. 95 F is the middle. */
    {-1.1111111, 71.1111111, "Intake air", "\260F"},
    /* 110 F to 280 F. 195 F is the middle. */
    {43.3333333, 137.7777778, "Transmission", "\260F"},
    /* Stored in bar. 2000 psi to 8000 psi, so 5000 psi is the middle. */
    {137.8947, 551.5788, "Fuel pressure", "psi"},
    /* Stored in bar. 0 to 90 psi, so 45 psi is the middle. */
    {0, 6.20505, "Oil pressure", "psi"},
    /* 140 F to 280 F. 210 F is the middle. */
    {60, 137.7777778, "Coolant", "\260F"}
};
