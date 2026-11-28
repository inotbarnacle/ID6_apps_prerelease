#include "warm_progress.h"
#include <assert.h>

int main() {
    WarmProgress progress;
    warm_frame data={{1000,1000,1000},{40,47,15}};
    assert(progress.update(data,7)<0);
    data.sample[0]=data.sample[1]=data.sample[2]=16000;
    data.celsius[0]=50;
    data.celsius[1]=52;
    data.celsius[2]=20;
    assert(progress.update(data,7)==69);
    assert(progress.update(data,3)<0);
    data.celsius[0]=85;
    data.celsius[1]=75;
    data.celsius[2]=40;
    assert(progress.update(data,7)==0);
    data.celsius[0]=82;
    assert(progress.update(data,7)==0);
    data.celsius[0]=80;
    assert(progress.update(data,7)<0);
    assert(progress.update(data,3)<0);
    // Missing oil cannot revoke the other fluids' own attained target state.
    progress.reset();
    data.celsius[WARM_OIL]=40;
    data.celsius[WARM_COOLANT]=75;
    data.celsius[WARM_TRANS]=40;
    assert(progress.update(data,6)<0);
    assert(!progress.targetReached(WARM_OIL));
    assert(progress.targetReached(WARM_COOLANT) && progress.targetReached(WARM_TRANS));
    data.celsius[WARM_COOLANT]=73;
    data.celsius[WARM_TRANS]=37;
    assert(progress.update(data,6)<0);
    assert(progress.targetReached(WARM_COOLANT) && progress.targetReached(WARM_TRANS));
    data.celsius[WARM_COOLANT]=69;
    assert(progress.update(data,6)<0 && !progress.targetReached(WARM_COOLANT));
    assert(progress.targetReached(WARM_TRANS));
    assert(!progress.targetReached(WARM_COUNT));
}
