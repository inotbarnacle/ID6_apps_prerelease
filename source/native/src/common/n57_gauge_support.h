#ifndef N57_GAUGE_SUPPORT_H
#define N57_GAUGE_SUPPORT_H
#include "gauge_core.h"
static inline int n57_gauge_supported(unsigned signal) {
    switch (signal) {
#define N57_GAUGE(slot, did, divisor) case BD_##slot: return 1;
#include "n57_gauges.def"
#undef N57_GAUGE
    default: return 0;
    }
}
#endif
