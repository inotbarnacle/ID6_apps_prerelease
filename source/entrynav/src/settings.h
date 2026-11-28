#ifndef ENTRYNAV_APPS_SETTINGS_H
#define ENTRYNAV_APPS_SETTINGS_H

// A plugin's settings: one short line per file in ENA_DATA_DIR, on the data partition.
#include "paths.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

// The file's first character, or fallback when it's missing or empty.
static inline char load_setting(const char* path, char fallback) {
    char line[8] = "";
    FILE* file = fopen(path, "r");
    if (file) {
        if (!fgets(line, sizeof line, file)) line[0] = 0;
        fclose(file);
    }
    return line[0] ? line[0] : fallback;
}

// Written whole and renamed into place, so a power cut leaves the old setting or the new one.
static inline void save_setting(const char* path, const char* line) {
    char temporary[192];
    if (snprintf(temporary, sizeof temporary, "%s.new", path) >= static_cast<int>(sizeof temporary)) return;
    mkdir(ENA_DATA_DIR, 0755);
    FILE* file = fopen(temporary, "w");
    if (!file) return;
    bool written = fputs(line, file) >= 0;
    written = fflush(file) == 0 && fsync(fileno(file)) == 0 && written;
    if (fclose(file) == 0 && written) rename(temporary, path);
    else unlink(temporary);
}

#endif
