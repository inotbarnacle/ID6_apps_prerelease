#include "power_history.h"
#include <assert.h>
#include <limits>
#include <unistd.h>

static PowerPoint point(uint64_t ms,double rpm=3000,double torque=500) {
    PowerPoint p={ms,rpm,torque,rpm*torque*6.283185307179586/60000};return p;
}

int main() {
    PowerHistory history;
    assert(history.push(point(1000)) && !history.push(point(1000)) && !history.push(point(999)));
    PowerPoint invalid=point(2000);invalid.rpm=std::numeric_limits<double>::quiet_NaN();
    assert(!history.push(invalid));
    invalid=point(2000);invalid.kw+=1;assert(!history.push(invalid));
    for(unsigned i=1;i<PowerHistory::Capacity;++i) assert(history.push(point(1000+i*250)));
    assert(!history.push(point(200000)) && history.size()==PowerHistory::Capacity);
    assert(history.push(point(200000),true) && history.at(0).ms==1250);
    char path[]="/tmp/id6-power-history-XXXXXX";int fd=mkstemp(path);assert(fd>=0);close(fd);
    assert(history.save(path));PowerHistory restored;assert(restored.load(path));
    assert(restored.size()==history.size() && restored.at(0).ms==0);
    assert(restored.at(599).ms==history.at(599).ms-history.at(0).ms);
    FILE* file=fopen(path,"ab");assert(file);fputs("bad row\n",file);fclose(file);
    assert(!restored.load(path) && restored.size()==600);unlink(path);
    const char* invalidTimes[]={"4294967296","-1","18446744073709551616","0junk"};
    for(unsigned i=0;i<4;++i) {
        file=fopen(path,"wb");assert(file);
        fprintf(file,"elapsed_ms,rpm,torque_nm,power_kw\n%s,3000,500,157.079633\n",invalidTimes[i]);fclose(file);
        assert(!restored.load(path));
    }
    unlink(path);
    history.clear();assert(!history.save(path));
    PowerPlot plot;
    for(unsigned i=0;i<49;++i) assert(history.push(point(1000+i*250,i*125)));
    plot.build(history,1000,13000);int a,b;
    assert(plot.segment(history,0,0,a,b) && a==64 && b==60);
    assert(plot.segment(history,0,PowerPlot::Segments-1,a,b) && b==0);
    history.clear();assert(history.push(point(1000)) && history.push(point(4000)));
    plot.build(history,1000,145000);assert(!plot.segment(history,0,0,a,b));
    history.clear();
    for(unsigned i=0;i<=20;++i) assert(history.push(point(1000+i*1000,1000+i*100)));
    plot.build(history,1000,21000);
    unsigned lines=0;
    for(unsigned i=0;i<PowerPlot::Segments;++i) if(plot.segment(history,0,i,a,b)) ++lines;
    assert(lines==PowerPlot::Segments && history.size()==21);
    bd_frame frame;memset(&frame,0,sizeof frame);frame.sent_ms=10000;
    const unsigned channels[]={BD_RPM,BD_TORQUE,BD_POWER};
    for(unsigned i=0;i<3;++i){frame.valid|=BD_BIT(channels[i]);frame.age_ms[channels[i]]=500;}
    frame.value[BD_RPM]=3000;frame.value[BD_TORQUE]=500;frame.value[BD_POWER]=point(0).kw;
    PowerPoint sample;
    assert(powerPointFromFrame(frame,10001,sample) && sample.ms==9500);
    frame.age_ms[BD_RPM]=499;assert(!powerPointFromFrame(frame,10001,sample));frame.age_ms[BD_RPM]=500;
    assert(!powerPointFromFrame(frame,13000,sample) && !powerPointFromFrame(frame,9999,sample));
    frame.demo=1;assert(!powerPointFromFrame(frame,10001,sample));
    puts("Power history: coherent samples, limits, CSV roundtrip/rejection, real-time bins and gaps OK");
}
