#ifndef ID6_WARM_UNITS_H
#define ID6_WARM_UNITS_H

#include "warm_frame.h"

static const double kWarmTarget[WARM_COUNT]={85,75,40};

static inline double warm_display_temperature(double celsius, int fahrenheit) {
    return fahrenheit ? celsius*1.8+32.0 : celsius;
}

#ifdef __cplusplus
#include "gauge_preferences.h"

static const char kWarmUnitsPath[]="/var/opt/hmi/data/ID5/WarmDeck.units";

inline bool loadWarmFahrenheit() {
    unsigned setting=0;
    bool fahrenheit=false;
    loadGaugePreferences(kWarmUnitsPath,1,setting,fahrenheit);
    return fahrenheit;
}

inline bool saveWarmFahrenheit(bool fahrenheit) {
    const unsigned char value[]={'G',1,0,static_cast<unsigned char>(fahrenheit)};
    return savePersistentBytes(kWarmUnitsPath,value,sizeof value);
}

inline const char* warmUnitsLabel(bool fahrenheit) {
    return fahrenheit ? "Temperature units: Fahrenheit" : "Temperature units: Celsius";
}
#endif

#endif
