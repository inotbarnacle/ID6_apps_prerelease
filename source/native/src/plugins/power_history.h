#ifndef ID6_POWER_HISTORY_H
#define ID6_POWER_HISTORY_H
#include "gauge_core.h"
#include "gauge_preferences.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

struct PowerPoint {
    uint64_t ms;
    double rpm, nm, kw;
};

inline bool validPowerPoint(const PowerPoint& p) {
    return p.rpm>=0 && p.rpm<=6000 && p.nm>=-300 && p.nm<=1000 &&
           p.kw>=-200 && p.kw<=700 &&
           fabs(p.kw-p.nm*p.rpm*(6.283185307179586/60000))<.01;
}

inline bool powerPointFromFrame(const bd_frame& frame, uint64_t now, PowerPoint& point) {
    if (frame.demo || !frame.sent_ms || frame.sent_ms>now) return false;
    const unsigned channels[]={BD_RPM,BD_TORQUE,BD_POWER};
    for (unsigned i=0;i<3;++i)
        if (!bd_is_fresh(&frame,channels[i],now-frame.sent_ms) ||
            frame.age_ms[channels[i]]!=frame.age_ms[BD_POWER]) return false;
    if (frame.age_ms[BD_POWER]>=frame.sent_ms) return false;
    PowerPoint candidate={frame.sent_ms-frame.age_ms[BD_POWER],frame.value[BD_RPM],
                          frame.value[BD_TORQUE],frame.value[BD_POWER]};
    if (!validPowerPoint(candidate)) return false;
    point=candidate;
    return true;
}

class PowerHistory {
public:
    enum { Capacity=600, MaxElapsed=3600000 };
private:
    PowerPoint points_[Capacity];
    unsigned count_;
public:
    PowerHistory():count_(0) {}
    void clear() { count_=0; }
    unsigned size() const { return count_; }
    const PowerPoint& at(unsigned index) const { return points_[index]; }
    bool push(const PowerPoint& point,bool rolling=false) {
        if (!validPowerPoint(point) || (count_ && point.ms<=points_[count_-1].ms)) return false;
        if (count_==Capacity) {
            if (!rolling) return false;
            memmove(points_,points_+1,sizeof(PowerPoint)*(Capacity-1));
            --count_;
        }
        points_[count_++]=point;
        return true;
    }
    bool save(const char* path) const {
        if (!count_) return false;
        const size_t capacity=Capacity*100u+100u;
        char* text=static_cast<char*>(malloc(capacity));
        if (!text) return false;
        size_t used=static_cast<size_t>(snprintf(text,capacity,"elapsed_ms,rpm,torque_nm,power_kw\n"));
        bool good=true;
        for (unsigned i=0;i<count_ && good;++i) {
            uint64_t elapsed=points_[i].ms-points_[0].ms;
            if (elapsed>MaxElapsed) { good=false; break; }
            int n=snprintf(text+used,capacity-used,"%u,%.6f,%.6f,%.6f\n",
                           static_cast<unsigned>(elapsed),points_[i].rpm,points_[i].nm,points_[i].kw);
            good=n>0 && static_cast<size_t>(n)<capacity-used;
            if (good) used+=static_cast<size_t>(n);
        }
        if (good) good=savePersistentBytes(path,reinterpret_cast<const unsigned char*>(text),used);
        free(text);
        return good;
    }
    bool load(const char* path) {
        FILE* file=fopen(path,"rb");
        if (!file) return false;
        PowerHistory loaded;
        char line[128];
        bool good=fgets(line,sizeof line,file) && !strcmp(line,"elapsed_ms,rpm,torque_nm,power_kw\n");
        while (good && fgets(line,sizeof line,file)) {
            char* end=0;
            errno=0;
            const unsigned long long elapsed=strtoull(line,&end,10);
            double rpm,nm,kw;
            char extra;
            if (!isdigit(static_cast<unsigned char>(line[0])) || errno || end==line || *end!=',' ||
                !strchr(line,'\n') || sscanf(end+1,"%lf,%lf,%lf %c",&rpm,&nm,&kw,&extra)!=3 ||
                elapsed>MaxElapsed || (!loaded.size() && elapsed!=0)) { good=false; break; }
            PowerPoint point={elapsed,rpm,nm,kw};
            good=loaded.push(point);
        }
        good=good && !ferror(file) && loaded.size()>0;
        fclose(file);
        if (good) *this=loaded;
        return good;
    }
};

struct PowerPlot {
    enum { Points=17, Segments=16, Height=64 };
    int index[Points];
    bool breakBefore[Points];
    PowerPoint columns[Points];
    uint64_t start,end;
    void build(const PowerHistory& history,uint64_t first,uint64_t last) {
        start=first;end=last>first ? last : first+1000;
        for (unsigned i=0;i<Points;++i) { index[i]=-1;breakBefore[i]=false; }
        /* Lines join real samples; interpolation affects pixels, never saved measurements. */
        for (unsigned column=0;column<Points;++column) {
            const uint64_t time=start+(end-start)*column/(Points-1);
            index[column]=-1;
            breakBefore[column]=false;
            for (unsigned i=1;i<history.size();++i) {
                const PowerPoint& a=history.at(i-1);
                const PowerPoint& b=history.at(i);
                const uint64_t span=b.ms-a.ms;
                if (column && span>=BD_STALE_MS) {
                    const uint64_t previous=start+(end-start)*(column-1)/(Points-1);
                    if (a.ms<time && b.ms>previous) breakBefore[column]=true;
                }
                if (span>=BD_STALE_MS || time<a.ms || time>b.ms) continue;
                const double fraction=static_cast<double>(time-a.ms)/span;
                PowerPoint value={time,a.rpm+(b.rpm-a.rpm)*fraction,
                    a.nm+(b.nm-a.nm)*fraction,a.kw+(b.kw-a.kw)*fraction};
                columns[column]=value;
                index[column]=static_cast<int>(i);
            }
        }
    }
    static int y(const PowerPoint& point,unsigned series) {
        double value=series==0 ? point.rpm : series==1 ? point.nm : point.kw;
        const double low=series==0 ? 0 : series==1 ? -300 : -200;
        const double high=series==0 ? 6000 : series==1 ? 1000 : 700;
        double fraction=(high-value)/(high-low);
        if (fraction<0) fraction=0;
        if (fraction>1) fraction=1;
        return static_cast<int>(fraction*Height+.5);
    }
    bool segment(const PowerHistory& history,unsigned series,unsigned segment,int& first,int& second) const {
        (void)history;
        if (series>=3 || segment>=Segments || index[segment]<0 || index[segment+1]<0 || breakBefore[segment+1]) return false;
        first=y(columns[segment],series);
        second=y(columns[segment+1],series);
        return true;
    }
};
#endif
