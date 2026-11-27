#ifndef ID6_SHIFT_WINDOW_H
#define ID6_SHIFT_WINDOW_H

#include "gauge_preferences.h"

struct ShiftWindow {
    enum { MinimumLower=3000, MaximumLower=5000, MaximumUpper=5500, Step=100,
           DefaultLower=4600, DefaultUpper=4800, Normal=0, Warning=1, Strong=2 };
    unsigned lower,upper;

    ShiftWindow():lower(DefaultLower),upper(DefaultUpper) {}
    bool valid() const {
        return lower>=MinimumLower && lower<=MaximumLower && upper<=MaximumUpper &&
               lower<upper && lower%Step==0 && upper%Step==0;
    }
    bool adjustLower(int change) {
        if (!valid() || change<-MaximumUpper || change>MaximumUpper) return false;
        const int value=static_cast<int>(lower)+change;
        if (value<MinimumLower || value>MaximumLower || value%Step ||
            static_cast<unsigned>(value)>=upper) return false;
        lower=static_cast<unsigned>(value);
        return true;
    }
    bool adjustUpper(int change) {
        if (!valid() || change<-MaximumUpper || change>MaximumUpper) return false;
        const int value=static_cast<int>(upper)+change;
        if (value<MinimumLower+Step || value>MaximumUpper || value%Step ||
            static_cast<unsigned>(value)<=lower) return false;
        upper=static_cast<unsigned>(value);
        return true;
    }
    unsigned signal(int rpm,bool fresh) const {
        if (!fresh || rpm<0) return Normal;
        return static_cast<unsigned>(rpm)>=upper ? Strong :
               static_cast<unsigned>(rpm)>=lower ? Warning : Normal;
    }
    bool load(const char* path) {
        FILE* file=fopen(path,"rb");
        if (!file) return false;
        unsigned char bytes[4];
        bool good=fread(bytes,1,sizeof bytes,file)==sizeof bytes && fgetc(file)==EOF && !ferror(file) &&
                  bytes[0]=='W' && bytes[1]==1;
        fclose(file);
        ShiftWindow candidate;
        if (good) {
            candidate.lower=bytes[2]*Step;
            candidate.upper=bytes[3]*Step;
            good=candidate.valid();
        }
        if (good) *this=candidate;
        return good;
    }
    bool save(const char* path) const {
        if (!valid()) return false;
        const unsigned char bytes[]={'W',1,static_cast<unsigned char>(lower/Step),
                                    static_cast<unsigned char>(upper/Step)};
        return savePersistentBytes(path,bytes,sizeof bytes);
    }
};

#endif
