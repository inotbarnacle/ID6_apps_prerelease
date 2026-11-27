#include "n57_dashboard_values.h"
#include <assert.h>
#include <stdio.h>

int main() {
    bd_frame frame;
    memset(&frame,0,sizeof frame);
    frame.sent_ms=1000;
    frame.seq=1;
    frame.valid=BD_BIT(BD_RPM)|BD_BIT(BD_TORQUE)|BD_BIT(BD_POWER)|BD_BIT(BD_BATTERY)|
                BD_BIT(BD_FUEL_VOLUME)|BD_BIT(BD_COOLANT)|BD_BIT(BD_OIL_TEMP);
    frame.value[BD_RPM]=800; frame.value[BD_TORQUE]=300;
    frame.value[BD_POWER]=25.132741; frame.value[BD_BATTERY]=14.2;
    frame.value[BD_FUEL_VOLUME]=58.5; frame.value[BD_COOLANT]=90; frame.value[BD_OIL_TEMP]=95;
    double value=0;
    assert(n57DashboardValue(frame,BD_POWER,1200,value) && value==frame.value[BD_POWER]);
    assert(n57DashboardValue(frame,BD_BATTERY,1200,value) && value==14.2);
    assert(!n57DashboardValue(frame,BD_RPM,999,value));
    assert(!n57DashboardValue(frame,BD_POWER,3500,value));
    assert(n57DashboardValue(frame,BD_BATTERY,4499,value));
    assert(!n57DashboardValue(frame,BD_BATTERY,4500,value));
    frame.age_ms[BD_TORQUE]=2400;
    assert(!n57DashboardValue(frame,BD_TORQUE,1100,value));
    frame.age_ms[BD_TORQUE]=0;
    frame.value[BD_RPM]=6001; assert(!n57DashboardValue(frame,BD_RPM,1000,value));
    frame.value[BD_RPM]=800;
    frame.value[BD_FUEL_VOLUME]=101; assert(!n57DashboardValue(frame,BD_FUEL_VOLUME,1000,value));
    frame.value[BD_FUEL_VOLUME]=58.5;
    frame.value[BD_POWER]=NAN; assert(!n57DashboardValue(frame,BD_POWER,1000,value));
    frame.value[BD_POWER]=25.132741;
    frame.valid &= ~BD_BIT(BD_OIL_TEMP); assert(!n57DashboardValue(frame,BD_OIL_TEMP,1000,value));
    assert(!n57DashboardValue(frame,BD_SPEED,1000,value));

    char packet[BD_MAX_PACKET];
    bd_frame parsed;
    int length=bd_encode(&frame,packet,sizeof packet);
    assert(length>0 && n57DashboardPacket(packet,length,1100,1000,parsed));
    assert(!n57DashboardPacket(packet,length,999,0,parsed));
    assert(!n57DashboardPacket(packet,length,1100,1001,parsed));
    packet[3]='8'; assert(!n57DashboardPacket(packet,length,1100,0,parsed));
    packet[3]='A';
    /* A real old NBG9 has 40 fields, not a relabelled 41-field NBGA. */
    char* tail=strrchr(packet,' '); assert(tail); *tail=0;
    tail=strrchr(packet,' '); assert(tail); *tail=0;
    assert(!n57DashboardPacket(packet,strlen(packet),1100,0,parsed));
    packet[3]='9';
    assert(n57DashboardPacket(packet,strlen(packet),1100,1000,parsed));
    assert(!(parsed.valid & BD_BIT(BD_REL_TORQUE_DDE)));
    frame.demo=1; length=bd_encode(&frame,packet,sizeof packet);
    assert(!n57DashboardPacket(packet,length,1100,0,parsed));
    frame.demo=0; frame.sent_ms=0; length=bd_encode(&frame,packet,sizeof packet);
    assert(!n57DashboardPacket(packet,length,1100,0,parsed));
    puts("N57 dashboards: fresh canonical values, unavailable/bounds, strict packet timing and no demo/legacy fallback OK");
}
