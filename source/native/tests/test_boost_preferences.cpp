#include "boost_preferences.h"
#include <assert.h>
#include <math.h>
#include <string.h>

int main() {
    char path[]="/tmp/boost-preferences.XXXXXX";
    int fd=mkstemp(path);
    assert(fd>=0);
    close(fd);
    const unsigned char legacy[]={'G',1,5,1};
    assert(savePersistentBytes(path,legacy,sizeof legacy));

    BoostPreferences prefs;
    prefs.load(path,9);
    assert(prefs.color==5 && prefs.choice[BOOST_STYLE]==0);
    assert(prefs.units(BD_SPEED)==1 && prefs.units(BD_COOLANT)==1 && prefs.units(BD_OIL)==1);
    prefs.choice[BOOST_PRESSURE]=2;
    prefs.choice[BOOST_SPEED]=0;
    prefs.choice[BOOST_STYLE]=1;
    prefs.choice[BOOST_COOLANT_ALERT]=9;
    prefs.choice[BOOST_OIL_TEMP_ALERT]=1;
    prefs.choice[BOOST_OIL_PRESSURE_ALERT]=4;
    prefs.choice[BOOST_CHIME]=1;
    assert(prefs.save(path));
    BoostPreferences restarted;
    restarted.load(path,9);
    assert(restarted.color==prefs.color && !memcmp(restarted.choice,prefs.choice,sizeof prefs.choice));
    assert(fabs(bd_display_selected(BD_BOOST,14.5037738,restarted.units(BD_BOOST))-100.0)<1e-6);
    assert(fabs(bd_display_selected(BD_SPEED,100,restarted.units(BD_SPEED))-62.1371192)<1e-6);
    assert(bd_display_selected(BD_COOLANT,110,restarted.units(BD_COOLANT))==110);
    assert(!strcmp(bd_unit_selected(BD_OIL,restarted.units(BD_OIL)),"kPa"));
    assert(fabs(bd_display_selected(BD_COOLANT,boostAlertThreshold(0,9),0)-260.0)<1e-6);

    prefs.choice[BOOST_OIL_PRESSURE_ALERT]=12;
    assert(prefs.save(path));
    restarted.load(path,9);
    prefs.choice[BOOST_OIL_PRESSURE_ALERT]=5;
    assert(!memcmp(restarted.choice,prefs.choice,sizeof prefs.choice));
    assert(fabs(bd_display_selected(BD_OIL,boostAlertThreshold(2,5),0)-25.0)<1e-6);

    unsigned char corrupted[3+BOOST_SETTING_COUNT]={'G',2,4};
    corrupted[3]=3;
    assert(savePersistentBytes(path,corrupted,sizeof corrupted));
    restarted.load(path,9);
    assert(restarted.color==prefs.color && !memcmp(restarted.choice,prefs.choice,sizeof prefs.choice));
    unlink(path);

    BoostAlerts alerts;
    bool entered=false;
    assert(!alerts.evaluate(0,true,true,110,105,100,entered) && !entered);
    assert(!alerts.evaluate(0,true,true,110,105,100,entered) && !entered);
    assert(alerts.evaluate(0,true,true,110,105,1500,entered) && entered);
    assert(alerts.evaluate(0,true,true,104,105,3200,entered) && !entered);
    assert(!alerts.evaluate(0,true,false,0,105,0,entered) && !entered);
    assert(alerts.evaluate(0,true,true,110,105,4400,entered) && !entered);
    assert(!alerts.evaluate(0,true,true,102,105,5500,entered) && !entered);
    assert(!alerts.evaluate(2,true,true,0.6,1.0,200,entered) && !entered);
    assert(alerts.evaluate(2,true,true,0.6,1.0,1400,entered) && entered);
    assert(alerts.evaluate(2,true,true,1.1,1.0,2700,entered) && !entered);
    assert(!alerts.evaluate(2,true,true,1.2,1.0,3900,entered) && !entered);
    puts("BoostDeck preferences, conversions and alert state passed.");
}
