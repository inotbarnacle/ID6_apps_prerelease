#ifndef ID6_GAUGE_PREFERENCES_H
#define ID6_GAUGE_PREFERENCES_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Keep preferences outside Plugins so replacing an app folder preserves them.
inline bool loadGaugePreferences(const char* path, unsigned limit, unsigned& setting, bool& metric) {
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    unsigned char data[4];
    bool valid = fread(data, 1, sizeof data, file) == sizeof data &&
                 fgetc(file) == EOF && !ferror(file) &&
                 data[0] == 'G' && data[1] == 1 && data[2] < limit && data[3] <= 1;
    fclose(file);
    if (!valid) return false;
    setting = data[2];
    metric = data[3] != 0;
    return true;
}

inline bool savePersistentBytes(const char* path, const unsigned char* data, size_t length) {
    char temporary[128];
    int size = snprintf(temporary, sizeof temporary, "%s.XXXXXX", path);
    if (size < 0 || static_cast<unsigned>(size) >= sizeof temporary) return false;
    int fd = mkstemp(temporary);
    if (fd < 0) return false;
    bool saved = write(fd, data, length) == static_cast<ssize_t>(length) && fsync(fd) == 0;
    if (close(fd) != 0) saved = false;
    if (saved && rename(temporary, path) == 0) return true;
    unlink(temporary);
    return false;
}

inline void saveGaugePreferences(const char* path, unsigned setting, bool metric) {
    const unsigned char data[] = {'G', 1, static_cast<unsigned char>(setting),
                                  static_cast<unsigned char>(metric)};
    savePersistentBytes(path, data, sizeof data);
}

#endif
