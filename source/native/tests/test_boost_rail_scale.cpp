#include "boost_rail_scale.h"
#include <assert.h>
#include <math.h>

int main() {
    char path[]="/tmp/boost-rail-scale.XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    BoostRailScale scale;
    assert(scale.maximum()==2000);
    const unsigned char pressure[]={0x27,0x10};
    double value=0;
    assert(bd_decode_obd(0x23,pressure,sizeof pressure,&value) && value==1000);
    assert(scale.bar(BD_RAIL,value,true)==500);
    assert(scale.bar(BD_RAIL,value,false)==1000);
    static const double expected[]={1000,1600,2000,2500};
    for (unsigned option=0;option<BoostRailScale::COUNT;++option) {
        scale.choice=option;
        assert(scale.maximum()==expected[option]);
        assert(scale.bar(BD_RAIL,0,true)==0);
        assert(scale.bar(BD_RAIL,expected[option]/2,true)==500);
        assert(scale.bar(BD_RAIL,expected[option],true)==1000);
        assert(scale.bar(BD_RAIL,expected[option]+100,true)==1000);
        assert(scale.bar(BD_RAIL,-1,true)==0);
        assert(scale.bar(BD_RAIL,NAN,true)==0);
        assert(scale.bar(BD_RAIL,INFINITY,true)==0);
        assert(scale.bar(BD_RAIL,-INFINITY,true)==0);
        // Every other gauge and the entire gasoline profile retain their ranges.
        for (unsigned signal=0;signal<BD_COUNT;++signal) {
            for (int n=-10;n<=100;++n) {
                const double sample=n*10.0;
                assert(scale.bar(signal,sample,false)==bd_bar(signal,sample));
                if (signal!=BD_RAIL) assert(scale.bar(signal,sample,true)==bd_bar(signal,sample));
            }
        }
        assert(scale.save(path));
        BoostRailScale restarted;
        restarted.load(path);
        assert(restarted.choice==option);
        for (unsigned units=0;units<3;++units) {
            const double top=bd_display_selected(BD_RAIL,scale.maximum(),units);
            const double half=bd_display_selected(BD_RAIL,scale.maximum()/2,units);
            assert(fabs(top-half*2)<1e-6);
        }
    }
    // Invalid/truncated/newer settings never replace the current valid choice.
    const unsigned char invalid[][4]={{'X',1,0,0},{'R',2,0,0},{'R',1,4,0},{'R',1,0,1}};
    scale.choice=1;
    for (unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i) {
        assert(savePersistentBytes(path,invalid[i],i==3 ? 4 : 3));
        scale.load(path);
        assert(scale.choice==1);
    }
    const unsigned char valid[]={'R',1,0};
    for (unsigned length=0;length<3;++length) {
        assert(savePersistentBytes(path,valid,length));
        scale.load(path);
        assert(scale.choice==1);
    }
    unlink(path);
    scale.load(path);
    assert(scale.choice==1);
    assert(!scale.save("/nonexistent-idrive-directory/scale"));
    scale.choice=4;
    assert(!scale.save(path));
    puts("Diesel rail display ranges, finite values, gasoline regressions and persistence passed.");
}
