#ifndef ID6_N57_DASHBOARD_VALUES_H
#define ID6_N57_DASHBOARD_VALUES_H

#include "gauge_core.h"
#include <math.h>
#include <string.h>

/* These dashboards require the pinned DDE worker, never demo/legacy OBD data. */
inline bool n57DashboardPacket(const char* bytes, size_t length, uint64_t now,
                               uint64_t previous, bd_frame& frame) {
    if (length<5 || (memcmp(bytes,"NBGA ",5) && memcmp(bytes,"NBG9 ",5)) || !bd_parse(bytes,length,&frame)) return false;
    return !frame.demo && frame.sent_ms && frame.sent_ms<=now && frame.sent_ms>=previous;
}

inline bool n57DashboardValue(const bd_frame& frame, unsigned signal,
                              uint64_t now, double& value) {
    if (!frame.sent_ms || frame.sent_ms>now || !bd_is_fresh(&frame,signal,now-frame.sent_ms)) return false;
    value=frame.value[signal];
    if (!isfinite(value)) return false;
    switch (signal) {
    case BD_RPM: return value>=0 && value<=6000;
    case BD_TORQUE: return value>=-300 && value<=1000;
    case BD_POWER: return value>=-190 && value<=630;
    case BD_BATTERY: return value>=6 && value<=18;
    case BD_FUEL_VOLUME: return value>=0 && value<=100;
    case BD_COOLANT: return value>=-40 && value<=150;
    case BD_OIL_TEMP: return value>=-50 && value<=180;
    default: return false;
    }
}

#endif
