#ifndef ID6_EMULATOR_BOOST_CONFIG_H
#define ID6_EMULATOR_BOOST_CONFIG_H
#include <stddef.h>

static inline int boost_config_valid(unsigned kind,const unsigned char* data,size_t count) {
    if (kind==0) {
        if (count==4) return data[0]=='G' && data[1]==1 && data[2]<9 && data[3]<=1;
        /* The loader clamps legacy beta oil-pressure choices 6..12. */
        static const unsigned char choices[]={3,2,2,2,2,10,10,13};
        if (count!=11 || data[0]!='G' || data[1]!=2 || data[2]>=9) return 0;
        for (unsigned i=0;i<8;++i) if (data[3+i]>=choices[i]) return 0;
        return 1;
    }
    if (kind==2) return count==3 && data[0]=='D' && data[1]==1 && data[2]<=1;
    if (kind!=1 || count<3 || data[0]!='B' || data[1]<1 || data[1]>6 || data[2]>1) return 0;
    const unsigned stride=data[1]==1 ? 11 : 9;
    if (count!=3+2*stride) return 0;
    const unsigned limits[]={0,31,31,39,47,55,58};
    for (unsigned profile=0;profile<2;++profile) for (unsigned i=0;i<9;++i)
        if (data[3+profile*stride+i]>=limits[data[1]]) return 0;
    return 1;
}
#endif
