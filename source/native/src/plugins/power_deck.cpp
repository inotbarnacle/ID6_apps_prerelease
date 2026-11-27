#include "n57_dashboard.h"
#include "power_history.h"
#include "power_rpm_plot.h"

class PowerDeck : public N57Dashboard {
    PowerHistory live_,recorded_,saved_;
    bool recording_;
    unsigned graph_;
    unsigned hidden_generation_;
    uint64_t graph_due_;
    enum { LineCount=PowerPlot::Segments*3 > PowerRpmPlot::Segments*2 ? PowerPlot::Segments*3 : PowerRpmPlot::Segments*2 };
    int line_image_[LineCount],line_y_[LineCount],line_height_[LineCount];
    char graph_caption_[192];
    char measurement_status_[64];
    const char* savedPath() const { return "/var/opt/hmi/data/ID5/PowerDeck.last.csv"; }
    void recordingChanged() {
        setUnfocusedCollection(recording_);
        setViewVisible(1u,viewActive(1u));
        setViewVisible(2u,viewActive(2u));
    }
    void invalidateGraph() {
        for(unsigned i=0;i<LineCount;++i) line_image_[i]=line_y_[i]=line_height_[i]=-1;
        graph_caption_[0]=0;graph_due_=0;
    }
    void property(unsigned component,unsigned key,unsigned value) {
        bmw::Parcel parcel;parcel.writeUInt32(value);setProperty(component,key,parcel);
    }
    bool saveMeasurement() {
        recording_=false;
        recordingChanged();
        const bool have_data=recorded_.size()!=0;
        bool saved=have_data && recorded_.save(savedPath());
        snprintf(measurement_status_,sizeof measurement_status_,"%s",!have_data ? "No data to save" : saved ? "Measurement saved" : "Could not save measurement");
        label(measurement_status_);
        return saved;
    }
    bool startMeasurement(const bmw::Parcel&) {
        if (graph_==111 || !collectionEnabled() || !viewShown(1u|2u)) return true;
        recorded_.clear();recording_=true;measurement_status_[0]=0;
        recordingChanged();render(bd_mono_ms());return true;
    }
    bool stopMeasurement(const bmw::Parcel&) { saveMeasurement();return true; }
    template <unsigned State,unsigned Key> bool graphActivity(const bmw::Map<unsigned,bmw::Variant>& event) {
        if (State==111 && Key==4u) {
            bool value=false;
            if (!hmiStateBool(event,Key,value)) setCollectionEnabled(false);
            return true; // Saved review stays read-only underneath its native OPTIONS.
        }
        const bool active=viewActivity(State==110 ? 2u : 4u,Key,event);
        const bool show=State==111 ? viewShown(4u) : active;
        if (State==111) {
            bool value=false;
            setCollectionEnabled(hmiStateBool(event,Key,value) && !show);
        }
        if (show) {
            graph_=State;invalidateGraph();
            if (State==111) {
                if (recording_) saveMeasurement();
                saved_.clear();saved_.load(savedPath());
            }
        } else if (graph_==State) graph_=0;
        setViewVisible(State==110 ? 2u : 4u,show,State==110);
        return true;
    }
    virtual void packetReceived(const bd_frame& frame) {
        PowerPoint sample;
        if (!powerPointFromFrame(frame,bd_mono_ms(),sample)) return;
        live_.push(sample,true);
        if (recording_) {
            recorded_.push(sample);
            if (recorded_.size()==PowerHistory::Capacity) saveMeasurement();
        }
    }
    virtual void viewChanged(bool show) {
        ++hidden_generation_;
        if (!show && recording_) {
            bmw::Parcel data;data.writeUInt32(0x50575356u);data.writeUInt32(hidden_generation_);
            post(bmw::Message(0x40000003u,data),1000u);
        }
    }
    void graphLine(unsigned index,unsigned component,unsigned model,int asset,int y,int height) {
        if (y!=line_y_[index]) { property(component,21u,static_cast<unsigned>(y));line_y_[index]=y; }
        if (height!=line_height_[index]) { property(component,10u,static_cast<unsigned>(height));line_height_[index]=height; }
        if (asset!=line_image_[index]) { updateModel(model,bmw::ImageID(static_cast<unsigned>(asset)));line_image_[index]=asset; }
    }
    void graphCaption(unsigned model,const char* text) {
        if (strcmp(text,graph_caption_)) {
            updateModel(model,bmw::BasicString<char>(text));
            snprintf(graph_caption_,sizeof graph_caption_,"%s",text);
        }
    }
    void graphPeak(unsigned series,const PowerRpmPlot& plot,bool show) {
        const unsigned component=4190u+series, model=2510u+series*10u;
        bmw::Parcel visible(show);
        setProperty(component,3u,visible);
        if (!show) {
            updateModel(model,bmw::BasicString<char>(""));
            return;
        }
        const int width=series ? 280 : 150;
        int x=plot.peakX(series)-width/2;
        const int right=PowerRpmPlot::Left+PowerRpmPlot::Segments*PowerRpmPlot::Step;
        if (x<PowerRpmPlot::Left) x=PowerRpmPlot::Left;
        if (x+width>right) x=right-width;
        const int point=plot.peakY(series), height=series ? 26 : 34;
        int y=series ? point+4 : point-height-4;
        if (y<PowerRpmPlot::Top) y=point+4;
        if (y+height>PowerRpmPlot::Top+PowerRpmPlot::Height) y=point-height-4;
        if (y<PowerRpmPlot::Top) y=PowerRpmPlot::Top;
        property(component,20u,static_cast<unsigned>(x));
        property(component,21u,static_cast<unsigned>(y));
        char text[48];
        if (series) snprintf(text,sizeof text,"%.0f kW / %.0f PS",plot.drawnPeakKw,
                             PowerRpmPlot::metricHorsepower(plot.drawnPeakKw));
        else snprintf(text,sizeof text,"%.0f Nm",plot.drawnPeakNm);
        updateModel(model,bmw::BasicString<char>(text));
    }
    void drawLiveGraph(uint64_t now) {
        PowerPlot plot;
        plot.build(live_,now>20000 ? now-20000 : 0,now);
        const unsigned top[]={20,130,240};
        for(unsigned series=0;series<3;++series) for(unsigned line=0;line<PowerPlot::Segments;++line) {
            const unsigned i=series*PowerPlot::Segments+line;
            int a=0,b=0;
            const bool valid=plot.segment(live_,series,line,a,b);
            int asset=61387,y=static_cast<int>(top[series]),height=3;
            if (valid) {
                asset=61000+static_cast<int>(series)*129+(b-a)+PowerPlot::Height;
                y+=a<b ? a : b;height=abs(b-a)+3;
            }
            graphLine(i,3000u+i,2000u+i,asset,y,height);
        }
        char caption[96];
        snprintf(caption,sizeof caption,live_.size() ? "Last 20 s | %s" : "Waiting for live samples",recording_ ? "Recording" : "Live");
        graphCaption(2300u,caption);
    }
    void drawSavedGraph() {
        if (*graph_caption_) return; /* The loaded CSV is immutable while review is open. */
        PowerRpmPlot plot;plot.build(saved_);
        for(unsigned series=0;series<2;++series) for(unsigned line=0;line<PowerRpmPlot::Segments;++line) {
            const unsigned i=series*PowerRpmPlot::Segments+line;
            int a=0,b=0,asset=64202,y=PowerRpmPlot::Top,height=3;
            if (plot.segment(series,line,a,b)) {
                asset=63000+static_cast<int>(series)*(PowerRpmPlot::Height*2+1)+(b-a)+PowerRpmPlot::Height;
                y+=a<b ? a : b;height=abs(b-a)+3;
            }
            graphLine(i,4000u+i,2400u+i,asset,y,height);
        }
        const bool drawable=plot.validRun && plot.drawableSegments()!=0;
        graphPeak(0,plot,drawable);
        graphPeak(1,plot,drawable && plot.drawnPeakKw<=plot.kwMax);
        updateModel(2499u,bmw::ImageID(64300u));
        char caption[192];
        if (!saved_.size()) snprintf(caption,sizeof caption,"No saved measurement");
        else if (!plot.validRun) snprintf(caption,sizeof caption,"Need a continuous rise in RPM");
        else if (!plot.drawableSegments()) snprintf(caption,sizeof caption,"Not enough RPM coverage");
        else if (plot.peakKw>plot.kwMax)
            snprintf(caption,sizeof caption,
                     "Peaks: %.0f Nm @ %.0f RPM | %.0f kW / %.0f PS (off scale)",
                     plot.peakNm,plot.peakNmRpm,plot.peakKw,
                     PowerRpmPlot::metricHorsepower(plot.peakKw));
        else snprintf(caption,sizeof caption,
                      "Peaks: %.0f Nm @ %.0f RPM | %.0f kW / %.0f PS @ %.0f RPM",
                      plot.peakNm,plot.peakNmRpm,plot.peakKw,
                      PowerRpmPlot::metricHorsepower(plot.peakKw),plot.peakKwRpm);
        graphCaption(2500u,caption);
    }
    void drawGraph(uint64_t now) {
        if (!graph_ || now<graph_due_) return;
        graph_due_=now+500; /* Bound RHMI updates independently from diagnostic sampling. */
        if (graph_==111) drawSavedGraph();
        else drawLiveGraph(now);
    }
    virtual void render(uint64_t now) {
        const unsigned signals[]={BD_RPM,BD_TORQUE,BD_POWER};
        bool any=false;
        for (unsigned i=0;i<3;++i) {
            double value=0;
            char text[16];
            if (available(signals[i],now,value)) {
                any=true;
                snprintf(text,sizeof text,i==2 ? "%.1f" : "%.0f",value);
            } else strcpy(text,"--");
            digits(60u+i*10u,6u,text,60010u);
        }
        double power=0;
        char horsepower[16];
        if (available(BD_POWER,now,power)) snprintf(horsepower,sizeof horsepower,"%.0f",PowerRpmPlot::metricHorsepower(power));
        else strcpy(horsepower,"--");
        digits(90u,6u,horsepower,60010u);
        if (recording_) label(any ? "REC | Recording" : "REC | No valid data");
        else if (*measurement_status_) label(measurement_status_);
        else label(any ? "Calculated from DDE torque" : "Data unavailable");
        drawGraph(now);
    }
public:
    // Only an explicit recording keeps polling through native OPTIONS or a visible preview.
    PowerDeck(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned major,unsigned minor,
                bmw::RHMIInterfaceVersion version,const bmw::Bundle& config,const bmw::Bundle& resources)
        : N57Dashboard(d,name,major,minor,version,config,resources,
              "/var/opt/hmi/data/ID5/Plugins/PowerDeck/boostdeck-collector-qnx",
              "/var/opt/hmi/data/ID5/Plugins/PowerDeck/gateway-ip.txt",42872u,
              BD_BIT(BD_RPM)|BD_BIT(BD_TORQUE)|BD_BIT(BD_POWER)),recording_(false),graph_(0),hidden_generation_(0),graph_due_(0) { measurement_status_[0]=0;invalidateGraph(); }
    virtual bool onFinalize() {
        if (recording_ && recorded_.size()) recorded_.save(savedPath());
        recording_=false;
        return N57Dashboard::onFinalize();
    }
    virtual bool invokeDataUpdate(unsigned key,const bmw::Parcel& data) {
        if (key!=0x50575356u) return N57Dashboard::invokeDataUpdate(key,data);
        unsigned generation;
        if (data.readUInt32(generation) && generation==hidden_generation_ && !visible_ && recording_) saveMeasurement();
        return true;
    }
    virtual bool onInit() {
        live_.clear();recorded_.clear();saved_.clear();recording_=false;graph_=0;measurement_status_[0]=0;invalidateGraph();
        if (!N57Dashboard::onInit()) return false;
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        Action action;action.m_obj=this;
        action.m_exec=&Action::execImpl<PowerDeck,&PowerDeck::startMeasurement>;addHandler(160u,action);
        action.m_exec=&Action::execImpl<PowerDeck,&PowerDeck::stopMeasurement>;addHandler(161u,action);
        typedef bmw::Delegate<bool,const bmw::Map<unsigned,bmw::Variant>&> StateAction;
        StateAction state;state.m_obj=this;
        state.m_exec=&StateAction::execImpl<PowerDeck,&PowerDeck::graphActivity<110,23> >;addHandlerWithMap(110u,11u,state);
        state.m_exec=&StateAction::execImpl<PowerDeck,&PowerDeck::graphActivity<110,4> >;addHandlerWithMap(110u,1u,state);
        state.m_exec=&StateAction::execImpl<PowerDeck,&PowerDeck::graphActivity<111,23> >;addHandlerWithMap(111u,11u,state);
        state.m_exec=&StateAction::execImpl<PowerDeck,&PowerDeck::graphActivity<111,4> >;addHandlerWithMap(111u,1u,state);
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo={1,"PowerDeck","N57",1,create_app<PowerDeck>,destroy_app};
}
