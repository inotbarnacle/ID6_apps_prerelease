#ifndef FAULT_PREFERENCES_H
#define FAULT_PREFERENCES_H

#include "gauge_preferences.h"
#include <errno.h>

struct FaultPreferences {
    bool raw;
    FaultPreferences() : raw(false) {} // preserve the legacy reference lookup until selected otherwise
    bool load(const char* path) {
        FILE* file=fopen(path,"rb");
        if (!file) {
            if (errno!=ENOENT) raw=true;
            return false;
        }
        raw=true; // damaged/unreadable settings must not select a reference dictionary
        unsigned char data[4];
        const size_t count=fread(data,1,sizeof data,file);
        bool complete=!ferror(file);
        if (complete && !feof(file)) complete=fgetc(file)==EOF && !ferror(file);
        fclose(file);
        if (!complete || count!=4 || data[0]!='E' || data[1]!=1 || data[2]>1 || data[3]) return false;
        raw=data[2]!=0;
        return true;
    }
    bool save(const char* path) const {
        const unsigned char data[]={'E',1,static_cast<unsigned char>(raw),0};
        return savePersistentBytes(path,data,sizeof data);
    }
};

#endif
