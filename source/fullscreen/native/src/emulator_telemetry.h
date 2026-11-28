#ifndef ID6_EMULATOR_TELEMETRY_H
#define ID6_EMULATOR_TELEMETRY_H

#include <stdint.h>

#define IDEMU_TELEMETRY_FILE "/dev/shmem/ID6LibraryLab-telemetry"

struct emulator_telemetry {
    uint64_t sampled_ms;
    int rpm, kph, coolant, pedal_tenths, gear, steering_hundredths, ignition;
};

#endif
