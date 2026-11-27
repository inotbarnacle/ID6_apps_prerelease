#ifndef BOOST_DEFAULTS_MODE_H
#define BOOST_DEFAULTS_MODE_H
#include "gauge_preferences.h"

struct BoostDefaultsMode {
    bool automatic;
    BoostDefaultsMode() : automatic(true) {}
    void load(const char* path) {
        if (!*path) return;
        FILE* file=fopen(path,"rb");
        if (!file) return;
        unsigned char bytes[3];
        const bool valid=fread(bytes,1,sizeof bytes,file)==sizeof bytes &&
            fgetc(file)==EOF && !ferror(file) && bytes[0]=='D' && bytes[1]==1 && bytes[2]<=1;
        fclose(file);
        if (valid) automatic=bytes[2]!=0;
    }
    bool save(const char* path) const {
        if (!*path) return false;
        const unsigned char bytes[]={'D',1,static_cast<unsigned char>(automatic)};
        return savePersistentBytes(path,bytes,sizeof bytes);
    }
};
#endif
