#ifndef BOOST_RAIL_SCALE_H
#define BOOST_RAIL_SCALE_H

#include "gauge_core.h"
#include "gauge_preferences.h"
#include <float.h>

// Display choices, not ECU limits or pressure targets. Values remain in bar.
struct BoostRailScale {
    enum { COUNT=4, DEFAULT=2 };
    unsigned choice;
    BoostRailScale() : choice(DEFAULT) {}

    double maximum() const {
        static const double values[]={1000,1600,2000,2500};
        return values[choice<COUNT ? choice : static_cast<unsigned>(DEFAULT)];
    }
    void load(const char* path) {
        FILE* file=fopen(path,"rb");
        if (!file) return;
        unsigned char data[3];
        const bool valid=fread(data,1,sizeof data,file)==sizeof data &&
            fgetc(file)==EOF && !ferror(file) && data[0]=='R' && data[1]==1 && data[2]<COUNT;
        fclose(file);
        if (valid) choice=data[2];
    }
    bool save(const char* path) const {
        if (choice>=COUNT) return false;
        const unsigned char data[]={'R',1,static_cast<unsigned char>(choice)};
        return savePersistentBytes(path,data,sizeof data);
    }
    int bar(unsigned signal, double value, bool diesel) const {
        if (!diesel || signal!=BD_RAIL) return bd_bar(signal,value);
        // Portable C++98 finite check, including both infinities and NaN.
        if (!(value>=0 && value<=DBL_MAX)) return 0;
        if (value>=maximum()) return 1000;
        return static_cast<int>(value*1000/maximum()+0.5);
    }
};

#endif
