#ifndef ID6_LAUNCH_RECORDS_H
#define ID6_LAUNCH_RECORDS_H

#include "launch_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Best and last 0-60 times in milliseconds, 0 meaning none.
// Kept outside Plugins so replacing the app folder preserves them.
struct LaunchRecords {
    uint32_t best_ms;
    uint32_t last_ms;
};

enum { kLaunchRecordBytes = 10 };

inline uint32_t readRecord(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
           static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
}

inline void writeRecord(unsigned char* data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<unsigned char>(value >> (8 * i));
}

inline bool plausibleRecord(uint32_t ms) { return ms == 0 || ms <= LT_TIMEOUT_MS; }

inline LaunchRecords loadLaunchRecords(const char* path) {
    LaunchRecords none = {0, 0};
    FILE* file = fopen(path, "rb");
    if (!file) return none;
    unsigned char data[kLaunchRecordBytes];
    bool read = fread(data, 1, sizeof data, file) == sizeof data && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!read || data[0] != 'L' || data[1] != 1) return none;
    LaunchRecords records = {readRecord(data + 2), readRecord(data + 6)};
    if (!plausibleRecord(records.best_ms) || !plausibleRecord(records.last_ms)) return none;
    return records;
}

inline bool saveLaunchRecords(const char* path, const LaunchRecords& records) {
    char temporary[128];
    int size = snprintf(temporary, sizeof temporary, "%s.XXXXXX", path);
    if (size < 0 || static_cast<unsigned>(size) >= sizeof temporary) return false;
    int fd = mkstemp(temporary);
    if (fd < 0) return false;
    unsigned char data[kLaunchRecordBytes] = {'L', 1};
    writeRecord(data + 2, records.best_ms);
    writeRecord(data + 6, records.last_ms);
    bool saved = write(fd, data, sizeof data) == static_cast<ssize_t>(sizeof data) && fsync(fd) == 0;
    if (close(fd) != 0) saved = false;
    if (!saved || rename(temporary, path) != 0) {
        unlink(temporary);
        return false;
    }
    return true;
}

// Records after a finished run: always the new last time, the best only when beaten.
inline LaunchRecords withFinishedRun(const LaunchRecords& records, uint32_t result_ms) {
    LaunchRecords next = {lt_is_better(result_ms, records.best_ms) ? result_ms : records.best_ms, result_ms};
    return next;
}

#endif
