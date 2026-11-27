#include "power_rpm_plot.h"
#include <assert.h>

static PowerPoint point(uint64_t ms,double rpm,double nm) {
    PowerPoint p={ms,rpm,nm,rpm*nm*6.283185307179586/60000};
    return p;
}

static void add(PowerHistory& history,uint64_t ms,double rpm,double nm=300) {
    assert(history.push(point(ms,rpm,nm)));
}

static bool near(double a,double b) { return fabs(a-b)<.000001; }

static void noDataAndReset() {
    PowerHistory history;
    PowerRpmPlot plot;
    int a=7,b=9;
    assert(!plot.validRun && plot.drawableSegments()==0);
    plot.build(history);
    assert(!plot.validRun && !plot.sampleCount && !plot.startMs && !plot.endMs);
    assert(!plot.segment(0,0,a,b) && a==7 && b==9);
    add(history,1000,1000);
    plot.build(history);
    assert(!plot.validRun);
    add(history,2000,1000);
    plot.build(history);
    assert(!plot.validRun); // A stationary RPM plateau is not a pull.
    add(history,3000,3000);
    plot.build(history);
    assert(plot.validRun && plot.drawableSegments()==8);
    history.clear();
    plot.build(history);
    assert(!plot.validRun && !plot.sampleCount && !plot.drawableSegments());
    for (unsigned i=0;i<PowerRpmPlot::Points;++i) assert(!plot.valid[i]);
    assert(plot.nmMax==1000 && plot.kwMax==400 && !plot.peakNm && !plot.peakKw);
    assert(!plot.peakNmRpm && !plot.peakKwRpm);
    assert(!plot.drawnPeakNm && !plot.drawnPeakKw);
}

static void fixedAxisAndSparseInterpolation() {
    assert(PowerRpmPlot::Points==25 && PowerRpmPlot::Segments==24);
    assert(PowerRpmPlot::Height==300 && PowerRpmPlot::Top==30);
    // Horizontal drawing belongs to the XML/assets generator, covered by
    // test_n57_dashboards.py; the sampler only maps RPM columns to Y values.
    PowerHistory history;
    add(history,1000,1000,100);
    add(history,2000,5000,550);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.validRun && plot.firstIndex==0 && plot.lastIndex==1 && plot.sampleCount==2);
    assert(plot.startMs==1000 && plot.endMs==2000);
    assert(plot.rpmMin==0 && plot.rpmMax==6000);
    assert(plot.nmMax==1000 && plot.kwMax==400);
    assert(near(plot.peakNm,550) && near(plot.peakKw,history.at(1).kw));
    assert(near(plot.peakNmRpm,5000) && near(plot.peakKwRpm,5000));
    assert(plot.drawableSegments()==16);
    for (unsigned i=0;i<PowerRpmPlot::Points;++i) assert(plot.valid[i]==(i>=4 && i<=20));
    assert(near(plot.columns[12].rpm,3000));
    assert(near(plot.columns[12].nm,325));
    assert(near(plot.columns[12].kw,(history.at(0).kw+history.at(1).kw)/2));
    assert(plot.columns[12].ms==1500);
    int a,b;
    assert(plot.segment(0,4,a,b) && a==270);
    assert(plot.segment(1,4,a,b) && a==292);
    assert(!plot.segment(0,3,a,b) && !plot.segment(0,20,a,b));
    assert(!plot.segment(2,4,a,b) && !plot.segment(0,24,a,b));
    // Plot interpolation must never alter the recorded measurements or CSV source.
    assert(history.size()==2 && history.at(0).ms==1000 && history.at(1).ms==2000);
    assert(near(history.at(0).nm,100) && near(history.at(1).nm,550));
}

static void selectOnePullAndLatestTie() {
    PowerHistory history;
    add(history,1000,1000);
    add(history,2000,4000);
    add(history,3000,1500);
    add(history,4000,3500);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.validRun && plot.firstIndex==0 && plot.lastIndex==1);
    assert(plot.sampleCount==2 && plot.endMs==2000); // Earlier run has the greatest span.
    add(history,5000,4500);
    plot.build(history);
    assert(plot.firstIndex==2 && plot.lastIndex==4 && plot.startMs==3000 && plot.endMs==5000);
    assert(plot.sampleCount==3); // Equal 3000 RPM span chooses the latest run.
    assert(!plot.valid[4] && plot.valid[6] && plot.valid[18] && !plot.valid[19]);

    history.clear();
    add(history,1000,3000);
    add(history,2000,2000);
    add(history,3000,1000);
    plot.build(history);
    assert(!plot.validRun && !plot.drawableSegments());
}

static void plateauUsesLatestWithoutAveraging() {
    PowerHistory history;
    add(history,1000,1000,700);
    add(history,1500,1000,200);
    add(history,2000,2000,300);
    add(history,2500,2000,400);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.validRun && plot.sampleCount==4 && plot.startMs==1000 && plot.endMs==2500);
    assert(near(plot.columns[4].nm,200) && plot.columns[4].ms==1500);
    assert(near(plot.columns[8].nm,400) && plot.columns[8].ms==2500);
    assert(near(plot.columns[6].nm,300) && plot.columns[6].ms==2000);
    assert(near(plot.columns[6].kw,(history.at(1).kw+history.at(3).kw)/2));
    assert(plot.nmMax==1000 && near(plot.peakNm,700)); // Peak still describes actual raw samples.
    assert(plot.drawableSegments()==4);
}

static void gapsSplitAtStaleThreshold() {
    PowerHistory history;
    add(history,1000,1000);
    add(history,1000+BD_STALE_MS-1,3000);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.validRun && plot.drawableSegments()==8);
    history.clear();
    add(history,1000,1000);
    add(history,1000+BD_STALE_MS,3000);
    plot.build(history);
    assert(!plot.validRun && !plot.drawableSegments());

    history.clear();
    add(history,1000,1000);
    add(history,2000,2000);
    add(history,2000+BD_STALE_MS,3000);
    add(history,3000+BD_STALE_MS,4500);
    plot.build(history);
    assert(plot.firstIndex==2 && plot.lastIndex==3 && plot.sampleCount==2);
    assert(!plot.valid[8] && !plot.valid[11] && plot.valid[12] && plot.valid[18]);
}

static void coastingSplitsAndAxesRespectLimits() {
    PowerHistory history;
    add(history,1000,1000);
    add(history,2000,2000);
    add(history,2500,2500,-100);
    add(history,3000,3000);
    add(history,4000,4000);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.firstIndex==3 && plot.lastIndex==4); // Negative torque/power split, latest equal span wins.
    assert(!plot.valid[8] && !plot.valid[10] && plot.valid[12]);
    history.clear();
    add(history,1000,1000,10);
    add(history,2000,2000,0);
    add(history,3000,3000,10);
    plot.build(history);
    assert(!plot.validRun); // Zero torque and power do not connect powered runs.

    history.clear();
    add(history,1000,0,100);
    add(history,2000,250,10);
    add(history,3000,500,20);
    plot.build(history);
    assert(plot.firstIndex==1 && plot.lastIndex==2 && plot.sampleCount==2);
    assert(!plot.valid[0] && plot.valid[1] && plot.valid[2]);
    assert(plot.nmMax==1000 && plot.kwMax==400);

    history.clear();
    add(history,1000,1,1);
    add(history,2000,6000,1000);
    plot.build(history);
    assert(plot.nmMax==1000 && plot.kwMax==400 && plot.peakKw>400);
    assert(!plot.valid[0] && plot.valid[24] && plot.drawableSegments()==23);
    int a,b;
    assert(plot.segment(0,23,a,b) && b==0);
    assert(!plot.segment(1,23,a,b)); // Above-range power is omitted, never flattened to 400.
    for (unsigned series=0;series<2;++series) {
        for (unsigned i=0;i<PowerRpmPlot::Segments;++i) {
            if (plot.segment(series,i,a,b)) assert(a>=0 && a<=300 && b>=0 && b<=300);
        }
    }
}

static void tinySpanDoesNotExtrapolate() {
    PowerHistory history;
    add(history,1000,1001);
    add(history,2000,1002);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.validRun && plot.sampleCount==2 && !plot.drawableSegments());
    for (unsigned i=0;i<PowerRpmPlot::Points;++i) assert(!plot.valid[i]);
    history.clear();
    add(history,1000,999);
    add(history,2000,1001);
    plot.build(history);
    assert(plot.validRun && plot.valid[4] && !plot.drawableSegments());
}

static void fullHistoryRemainsOneRun() {
    PowerHistory history;
    for (unsigned i=0;i<PowerHistory::Capacity;++i) add(history,1000+i,1000+i,100+i);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.validRun && plot.sampleCount==PowerHistory::Capacity);
    assert(plot.firstIndex==0 && plot.lastIndex==PowerHistory::Capacity-1);
    assert(plot.startMs==1000 && plot.endMs==1599 && plot.drawableSegments()==2);
}

static void powerAxisAndMetricHorsepower() {
    PowerHistory history;
    add(history,1000,1000,500);
    add(history,2000,3000,500);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.nmMax==1000 && plot.kwMax==400);
    assert(plot.peakKw>150 && plot.peakKw<200);
    const double factor=0.73549875;
    assert(near(PowerRpmPlot::metricHorsepower(0),0));
    assert(near(PowerRpmPlot::metricHorsepower(factor),1));
    assert(near(PowerRpmPlot::metricHorsepower(257.4245625),350));
    assert(near(PowerRpmPlot::metricHorsepower(-257.4245625),-350));
    for (unsigned ps=0;ps<=1000;ps+=25) {
        assert(near(PowerRpmPlot::metricHorsepower(ps*factor),ps));
        // Both power unit axes must locate the same physical value at the same Y.
        const double kw=ps*factor;
        const double kwY=PowerRpmPlot::Height*(1-kw/plot.kwMax);
        const double psY=PowerRpmPlot::Height*(1-PowerRpmPlot::metricHorsepower(kw)*factor/plot.kwMax);
        assert(near(kwY,psY));
    }
    history.clear();
    add(history,1000,1000,550);
    add(history,2000,4000,550);
    plot.build(history);
    assert(plot.kwMax==400 && plot.peakKw>225 && plot.peakKw<250);
}

static void peakLocationsUseRawSamples() {
    PowerHistory history;
    add(history,1000,1000,400);
    add(history,2000,2000,600);
    add(history,3000,3000,600);
    add(history,4000,4000,500);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.validRun && near(plot.peakNm,600) && near(plot.peakNmRpm,3000));
    assert(near(plot.peakKw,history.at(3).kw) && near(plot.peakKwRpm,4000));
    assert(plot.peakX(0)==PowerRpmPlot::Left+12*PowerRpmPlot::Step);
    assert(plot.peakX(1)==PowerRpmPlot::Left+16*PowerRpmPlot::Step);
    assert(plot.peakY(0)==150);
    assert(plot.peakY(1)>=172 && plot.peakY(1)<=174);
}

static void offGridRawSpikeDoesNotFloatLabel() {
    PowerHistory history;
    add(history,1000,3000,200);
    add(history,2000,3125,900);
    add(history,3000,3250,200);
    PowerRpmPlot plot;
    plot.build(history);
    assert(plot.validRun && near(plot.peakNm,900) && near(plot.peakNmRpm,3125));
    assert(near(plot.drawnPeakNm,200) && near(plot.drawnPeakNmRpm,3250));
    assert(plot.peakX(0)==PowerRpmPlot::Left+13*PowerRpmPlot::Step);
    assert(plot.peakY(0)==270); /* Label belongs to the visible 200-Nm line. */
}

int main() {
    noDataAndReset();
    fixedAxisAndSparseInterpolation();
    selectOnePullAndLatestTie();
    plateauUsesLatestWithoutAveraging();
    gapsSplitAtStaleThreshold();
    coastingSplitsAndAxesRespectLimits();
    tinySpanDoesNotExtrapolate();
    fullHistoryRemainsOneRun();
    powerAxisAndMetricHorsepower();
    peakLocationsUseRawSamples();
    offGridRawSpikeDoesNotFloatLabel();
    puts("Saved RPM plot: truthful pull, fixed 400 kW, metric PS, over-range gap and no extrapolation OK");
}
