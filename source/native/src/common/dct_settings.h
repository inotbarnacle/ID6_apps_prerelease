#ifndef ID6_DCT_SETTINGS_H
#define ID6_DCT_SETTINGS_H

#include "dct_temperature.h"
#include "gauge_preferences.h"

inline bool toggleDctTemperature() {
    const unsigned char next=dct_temperature_enabled() ? '0' : '1';
    return savePersistentBytes(ID6_DCT_PREFERENCE,&next,1);
}

inline const char *dctTemperatureLabel() {
    return dct_temperature_enabled() ? "DCT temperature (experimental): On" :
                                       "DCT temperature (experimental): Off";
}

#endif
