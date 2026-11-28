#ifndef ID6_LIBRARY_BOOST_LAYOUT_H
#define ID6_LIBRARY_BOOST_LAYOUT_H

#include "boost_preferences.h"
#include <string.h>

struct BoostLayout {
    unsigned char signal[2][9];
    bool diesel;

    BoostLayout() : diesel(false) {
        static const unsigned char defaults[] = {
            BD_BOOST, BD_LOAD, BD_RPM, BD_IAT, BD_TRANS, BD_RAIL, BD_MAF, BD_COOLANT, BD_MAP
        };
        for (unsigned i=0; i<9; ++i) signal[0][i]=static_cast<unsigned char>(i);
        memcpy(signal[1],defaults,sizeof defaults);
    }

    bool load(const char* path) {
        FILE* file=fopen(path,"rb");
        if (!file) return false;
        unsigned char data[25];
        const size_t len=fread(data,1,sizeof data,file);
        bool complete=!ferror(file);
        if (complete && !feof(file)) complete=fgetc(file)==EOF && !ferror(file);
        fclose(file);
        if (!complete || len<3 || data[0]!='B' || data[2]>1) return false;
        const unsigned stride=data[1]>=2 && data[1]<=6 && len==21 ? 9 : data[1]==1 && len==25 ? 11 : 0;
        if (!stride) return false;
        const unsigned channels=data[1]==6 ? BD_WIRE11_COUNT : data[1]==5 ? BD_WIRE10_COUNT :
            data[1]==4 ? BD_WIRE9_COUNT : data[1]==3 ? BD_WIRE8_COUNT : BD_LEGACY_COUNT;
        for (unsigned profile=0; profile<2; ++profile)
            for (unsigned i=0; i<9; ++i)
                if (data[3+profile*stride+i]>=channels) return false;
        for (unsigned profile=0; profile<2; ++profile)
            memcpy(signal[profile],data+3+profile*stride,9);
        diesel=data[2]!=0;
        return true;
    }

    bool save(const char* path) const {
        unsigned char data[21]={'B',2,static_cast<unsigned char>(diesel)};
        unsigned maximum=0;
        for (unsigned profile=0;profile<2;++profile) for (unsigned i=0;i<9;++i) {
            if (signal[profile][i]>=BD_COUNT) return false;
            if (signal[profile][i]>maximum) maximum=signal[profile][i];
        }
        data[1]=maximum>=BD_WIRE10_COUNT ? 6 : maximum>=BD_WIRE9_COUNT ? 5 :
                maximum>=BD_WIRE8_COUNT ? 4 : maximum>=BD_LEGACY_COUNT ? 3 : 2;
        memcpy(data+3,signal,sizeof signal);
        return saveFullScreenRecord(path,data,sizeof data);
    }
};
#endif
