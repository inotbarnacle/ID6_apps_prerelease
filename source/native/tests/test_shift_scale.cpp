#include "shift_scale.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main() {
    assert(shift_scale_max(true)==6000u && shift_scale_max(false)==8000u);
    const unsigned steps[]={32u,128u};
    for (unsigned i=0;i<2;++i) {
        const unsigned resolution=steps[i];
        assert(shift_scale_step(-1,true,resolution)==0);
        assert(shift_scale_step(0,true,resolution)==0);
        assert(shift_scale_step(750,true,resolution)==resolution/8u);
        assert(shift_scale_step(3000,true,resolution)==resolution/2u);
        assert(shift_scale_step(6000,true,resolution)==resolution);
        assert(shift_scale_step(INT_MAX,true,resolution)==resolution);
        assert(shift_scale_step(4000,false,resolution)==resolution/2u);
        assert(shift_scale_step(8000,false,resolution)==resolution);
        unsigned previous=0;
        for (int rpm=0;rpm<=9000;++rpm) {
            const unsigned current=shift_scale_step(rpm,true,resolution);
            assert(current>=previous && current<=resolution);
            previous=current;
        }
    }
    assert(shift_scale_step(4000,true,128u)==85u);
    assert(shift_scale_step(4500,true,128u)==96u);
    for (unsigned i=0;i<8;++i) {
        assert(shift_target_allowed(i,false));
        assert(shift_target_allowed(i,true)==(i>=5u));
    }
    assert(!shift_target_allowed(8u,false) && !shift_target_allowed(UINT_MAX,true));
    puts("Shift scale: N57 0–6000, gasoline 0–8000, both bar resolutions, clamping and target profiles OK");
}
