#ifndef ID6_LAUNCH_RECORDS_H
#define ID6_LAUNCH_RECORDS_H

#include "launch_timer.h"
#include "gauge_preferences.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Best and last 0-60 times in milliseconds, 0 meaning none.
// Kept outside Plugins so replacing the app folder preserves them.
struct LaunchRecords {
    uint32_t best_ms;
    uint32_t last_ms;
    uint32_t previous_ms;
    uint32_t third_ms;
};

enum { kLaunchRecordBytes = 18 };

inline uint32_t readRecord(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 |
           static_cast<uint32_t>(data[2]) << 16 | static_cast<uint32_t>(data[3]) << 24;
}

inline void writeRecord(unsigned char* data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[i] = static_cast<unsigned char>(value >> (8 * i));
}

inline bool plausibleRecord(uint32_t ms) { return ms == 0 || ms <= LT_TIMEOUT_MS; }

inline LaunchRecords loadLaunchRecords(const char* path) {
    LaunchRecords none = {0, 0, 0, 0};
    FILE* file = fopen(path, "rb");
    if (!file) return none;
    unsigned char data[kLaunchRecordBytes]={0};
    size_t length=fread(data,1,sizeof data,file);
    bool read=fgetc(file)==EOF && !ferror(file);
    fclose(file);
    if (!read || data[0]!='L' ||
        !((length==10 && data[1]==1) || (length==sizeof data && data[1]==2))) return none;
    LaunchRecords records={readRecord(data+2),readRecord(data+6),readRecord(data+10),readRecord(data+14)};
    if (!plausibleRecord(records.best_ms) || !plausibleRecord(records.last_ms) ||
        !plausibleRecord(records.previous_ms) || !plausibleRecord(records.third_ms)) return none;
    return records;
}

inline bool saveLaunchRecords(const char* path, const LaunchRecords& records) {
    unsigned char data[kLaunchRecordBytes]={'L',2};
    writeRecord(data+2,records.best_ms);
    writeRecord(data+6,records.last_ms);
    writeRecord(data+10,records.previous_ms);
    writeRecord(data+14,records.third_ms);
    return savePersistentBytes(path,data,sizeof data);
}

// Records after a finished run: always the new last time, the best only when beaten.
inline LaunchRecords withFinishedRun(const LaunchRecords& records, uint32_t result_ms) {
    LaunchRecords next = {lt_is_better(result_ms, records.best_ms) ? result_ms : records.best_ms, result_ms, records.last_ms, records.previous_ms};
    return next;
}

#endif
