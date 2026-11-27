#ifndef ID6_POWER_RPM_PLOT_H
#define ID6_POWER_RPM_PLOT_H

#include "power_history.h"

/* A saved chart represents one acceleration run, without joining separate pulls. */
struct PowerRpmPlot {
    enum { Points=25, Segments=24, Height=300, Top=30, Left=70, Step=42 };
    bool validRun;
    unsigned firstIndex,lastIndex,sampleCount;
    uint64_t startMs,endMs;
    unsigned rpmMin,rpmMax,nmMax,kwMax;
    double peakNm,peakKw,peakNmRpm,peakKwRpm;
    double drawnPeakNm,drawnPeakKw,drawnPeakNmRpm,drawnPeakKwRpm;
    bool valid[Points];
    PowerPoint columns[Points];

    PowerRpmPlot() { reset(); }

    /* Metric horsepower (PS): 75 kgf m/s, with standard gravity 9.80665 m/s2. */
    static double metricHorsepower(double kw) { return kw/0.73549875; }

    void build(const PowerHistory& history) {
        reset();
        unsigned first=0;
        bool active=false;
        for (unsigned i=0;i<history.size();++i) {
            const PowerPoint& point=history.at(i);
            if (point.nm<=0 || point.kw<=0) {
                if (active) consider(history,first,i-1);
                active=false;
                continue;
            }
            if (active && (point.rpm<history.at(i-1).rpm ||
                           point.ms-history.at(i-1).ms>=BD_STALE_MS)) {
                consider(history,first,i-1);
                active=false;
            }
            if (!active) { first=i; active=true; }
        }
        if (active) consider(history,first,history.size()-1);
        if (!validRun) return;

        sampleCount=lastIndex-firstIndex+1;
        startMs=history.at(firstIndex).ms;
        endMs=history.at(lastIndex).ms;
        for (unsigned i=firstIndex;i<=lastIndex;++i) {
            if (history.at(i).nm>=peakNm) {
                peakNm=history.at(i).nm;
                peakNmRpm=history.at(i).rpm;
            }
            if (history.at(i).kw>=peakKw) {
                peakKw=history.at(i).kw;
                peakKwRpm=history.at(i).rpm;
            }
        }
        for (unsigned column=0;column<Points;++column) {
            const double rpm=static_cast<double>(rpmMax)*column/Segments;
            if (rpm<history.at(firstIndex).rpm || rpm>history.at(lastIndex).rpm) continue;

            /* Use the latest value on each equal-RPM plateau, without averaging. */
            unsigned lower=firstIndex;
            while (lower<lastIndex && history.at(lower+1).rpm<=rpm) ++lower;
            const PowerPoint& a=history.at(lower);
            if (a.rpm==rpm) {
                columns[column]=a;
                valid[column]=true;
                continue;
            }
            unsigned upper=lower+1;
            while (upper<lastIndex && history.at(upper+1).rpm==history.at(upper).rpm) ++upper;
            const PowerPoint& b=history.at(upper);
            const double fraction=(rpm-a.rpm)/(b.rpm-a.rpm);
            PowerPoint value={a.ms+static_cast<uint64_t>((b.ms-a.ms)*fraction),rpm,
                a.nm+(b.nm-a.nm)*fraction,a.kw+(b.kw-a.kw)*fraction};
            columns[column]=value;
            valid[column]=true;
        }
        for (unsigned column=0;column<Points;++column) {
            if (!valid[column]) continue;
            if (columns[column].nm>=drawnPeakNm) {
                drawnPeakNm=columns[column].nm;
                drawnPeakNmRpm=columns[column].rpm;
            }
            if (columns[column].kw>=drawnPeakKw) {
                drawnPeakKw=columns[column].kw;
                drawnPeakKwRpm=columns[column].rpm;
            }
        }
    }

    unsigned drawableSegments() const {
        unsigned count=0;
        for (unsigned i=0;i<Segments;++i) if (valid[i] && valid[i+1]) ++count;
        return count;
    }

    bool segment(unsigned series,unsigned index,int& first,int& second) const {
        if (series>=2 || index>=Segments || !valid[index] || !valid[index+1]) return false;
        if (series==1 && (columns[index].kw>kwMax || columns[index+1].kw>kwMax)) return false;
        first=y(columns[index],series);
        second=y(columns[index+1],series);
        return true;
    }

    int peakX(unsigned series) const {
        double rpm=series ? drawnPeakKwRpm : drawnPeakNmRpm;
        if (rpm<0) rpm=0;
        if (rpm>rpmMax) rpm=rpmMax;
        return Left+static_cast<int>(rpm*Segments*Step/rpmMax+.5);
    }

    int peakY(unsigned series) const {
        PowerPoint peak={0,0,drawnPeakNm,drawnPeakKw};
        return Top+y(peak,series);
    }

private:
    void reset() {
        validRun=false;
        firstIndex=lastIndex=sampleCount=0;
        startMs=endMs=0;
        rpmMin=0; rpmMax=6000; nmMax=1000; kwMax=400;
        peakNm=peakKw=peakNmRpm=peakKwRpm=0;
        drawnPeakNm=drawnPeakKw=drawnPeakNmRpm=drawnPeakKwRpm=0;
        for (unsigned i=0;i<Points;++i) {
            valid[i]=false;
            PowerPoint empty={0,0,0,0};
            columns[i]=empty;
        }
    }

    void consider(const PowerHistory& history,unsigned first,unsigned last) {
        const double span=history.at(last).rpm-history.at(first).rpm;
        if (last<=first || span<=0) return;
        const double bestSpan=validRun ? history.at(lastIndex).rpm-history.at(firstIndex).rpm : -1;
        if (span>bestSpan || (span==bestSpan && history.at(last).ms>history.at(lastIndex).ms)) {
            validRun=true;
            firstIndex=first;
            lastIndex=last;
        }
    }

    int y(const PowerPoint& point,unsigned series) const {
        const double value=series ? point.kw : point.nm;
        const unsigned maximum=series ? kwMax : nmMax;
        double fraction=(maximum-value)/maximum;
        if (fraction<0) fraction=0;
        if (fraction>1) fraction=1;
        return static_cast<int>(fraction*Height+.5);
    }
};

#endif
