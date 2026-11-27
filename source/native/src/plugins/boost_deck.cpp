// Native NBT EVO RA plugin. Models are touched only on the HMI dispatcher.
// Network diagnostics run in the separate collector process, never in HmiMain3D.
#include "plugin_factory.h"
#include "gauge_core.h"
#include "dial_scales.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include "boost_preferences.h"
#include "boost_rail_scale.h"
#include "boostdeck_assignments.h"
#include "hmi_state_activity.h"
#include "n57.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__arm__)
// C++98 target ABI gates. Host builds have 64-bit pointers and cannot prove these.
typedef char bd_pointer_size[(sizeof(void*)==4) ? 1 : -1];
typedef char bd_parcel_size[(sizeof(bmw::Parcel)==20) ? 1 : -1];
typedef char bd_message_size[(sizeof(bmw::Message)==80 && __alignof__(bmw::Message)==8) ? 1 : -1];
typedef char bd_std_string_size[(sizeof(std::string)==4) ? 1 : -1];
typedef char bd_delegate_size[(sizeof(bmw::Delegate<bool,const bmw::Parcel&>)==8) ? 1 : -1];
#endif

namespace {
// In the installed AppFramework, this message reads a UInt32 key from its
// Parcel and dispatches through the primary invokeDataUpdate virtual.
const unsigned kDataUpdateMessage = 0x40000003u;
const unsigned kPollKey = 0x4244504cu; // BDPL, private to this app
const unsigned kActivePollIntervalMs = 25u;
const unsigned kIdlePollIntervalMs = 250u;
const char kCollector[] = "/var/opt/hmi/data/ID5/Plugins/BoostDeck/boostdeck-collector-qnx";
const char kPreferences[] = "/var/opt/hmi/data/ID5/BoostDeck.prefs";
const char kRailScalePreferences[] = "/var/opt/hmi/data/ID5/BoostDeck.rail-scale";
const unsigned kRailScaleRow = BOOST_SETTING_COUNT;
const unsigned kBarCount = 9;
const unsigned kVerticalSlot = 8;
const char* const kGaugeCaptions[][2] = {
#define SIGNAL(id, name, lo, hi, unit, wide, narrow) {wide, narrow},
#include "boost_signals.def"
#undef SIGNAL
};
const unsigned kVerticalFirst = 56100;
const unsigned kVerticalSteps = 20;
const unsigned kTransparent = 57000;
const unsigned kDialFirst = 57001;
const unsigned kCircularSteps = 12;
const unsigned kAlertSignals[] = {BD_COOLANT, BD_OIL_TEMP, BD_OIL};
const char* const kSettingLabels[] = {
    "Pressure", "Speed", "Temperature", "Gauge style", "Alert mode",
    "Coolant high", "Oil temp high", "Oil pressure low"
};

void settingValue(const BoostPreferences& prefs, unsigned row, unsigned choice, char* text, size_t size) {
    static const char* const values[][3]={
        {"psi","bar","SI (kPa / MPa)"}, {"mph","km/h",0}, {"\260F","\260C",0},
        {"Bars","Circular dials",0}, {"Screen + soft chime","Screen only",0},
    };
    if (row<BOOST_COOLANT_ALERT) snprintf(text,size,"%s",values[row][choice]);
    else if (!choice) snprintf(text,size,"Off");
    else {
        const unsigned alarm=row-BOOST_COOLANT_ALERT, signal=kAlertSignals[alarm];
        const unsigned units=prefs.units(signal);
        const double value=bd_display_selected(signal,boostAlertThreshold(alarm,choice),units);
        const int decimals=units==1 ? (alarm==2 ? 2 : 1) : 0;
        snprintf(text,size,"%.*f %s",decimals,value,bd_unit_selected(signal,units));
    }
}
const char kAssignments[] = "/var/opt/hmi/data/ID5/BoostDeck.signals";

}

class BoostDeck : public bmw::PluginApp {
    int fd_;
    unsigned poll_generation_;
    bd_frame frame_;
    uint64_t received_;
    pid_t collector_;
    bool stopping_;
    bool gauges_visible_;
    unsigned shown_,focused_,n57_index_;
    BoostPreferences preferences_;
    BoostRailScale rail_scale_;
    BoostAlerts alerts_;
    pid_t tone_;
    uint64_t last_tone_;
    unsigned alert_mask_;
    uint64_t retry_at_;
    unsigned char signals_[2][kBarCount];
    bool diesel_;
    bool hide_emissions_;
    unsigned editing_;
    void loadAssignments();
    void updateAssignments();
    bool saveAssignments();
    void assignmentsChanged();
    void startVisibleCollector();
    void updateSettings();
    void updateAlerts(uint64_t now, uint64_t elapsed);
    void stopTone();
    void reapTone();
    void restartCollector();
    char labels_[kBarCount][128];
    char vertical_unit_[32];
    bool unit_dirty_;
    unsigned images_[kBarCount], dials_[kBarCount];
    void postPoll();
public:
    BoostDeck(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned maj, unsigned mn,
              bmw::RHMIInterfaceVersion rhmi, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : bmw::PluginApp(d,name,maj,mn,rhmi,cfg,res), fd_(-1), poll_generation_(0),
          received_(0), collector_(-1), stopping_(false), gauges_visible_(false),
          shown_(0),focused_(0),n57_index_(0),tone_(-1), last_tone_(0), alert_mask_(0),
          retry_at_(0), diesel_(false),hide_emissions_(false), editing_(kBarCount) {
        resetDisplay();
    }
    virtual ~BoostDeck() { shutdown(); }
    virtual bool onInit();
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data);
    template <unsigned Key> bool gaugeActivity(const bmw::Map<unsigned,bmw::Variant>&);
    template <unsigned Color> bool changeColor(const bmw::Parcel&) {
        BoostPreferences changed=preferences_;
        changed.color=Color;
        if (changed.save(kPreferences)) {
            preferences_=changed;
            updateModel(118u,bmw::BasicString<char>(""));
        } else updateModel(118u,bmw::BasicString<char>("Could not save settings"));
        updateSettings();
        refresh();
        return true;
    }
    template <unsigned Row, unsigned Choice> bool changeSetting(const bmw::Parcel&) {
        if (preferences_.choice[Row]==Choice) return true;
        BoostPreferences changed=preferences_;
        changed.choice[Row]=Choice;
        if (!changed.save(kPreferences)) {
            updateModel(118u,bmw::BasicString<char>("Could not save settings"));
            return true;
        }
        preferences_=changed;
        if (Row>=BOOST_COOLANT_ALERT) {
            alerts_.reset(Row-BOOST_COOLANT_ALERT);
            restartCollector();
        }
        updateModel(118u,bmw::BasicString<char>(""));
        updateSettings();
        updateAssignments();
        return true;
    }
    template <unsigned Choice> bool changeRailScale(const bmw::Parcel&) {
        BoostRailScale changed=rail_scale_;
        changed.choice=Choice;
        if (!changed.save(kRailScalePreferences)) {
            updateModel(118u,bmw::BasicString<char>("Could not save diesel rail scale"));
            return true;
        }
        rail_scale_=changed;
        updateModel(118u,bmw::BasicString<char>(""));
        updateSettings();
        updateAssignments();
        return true;
    }
    template <unsigned Slot> bool editGauge(const bmw::Map<unsigned,bmw::Variant>& event) {
        bool show;
        if (hmiStateBool(event,23u,show)) {
            if (show) {
                editing_=Slot;
                for (unsigned signal=0;signal<BD_COUNT;++signal)
                    updateModel(5300u+signal,signals_[diesel_][Slot]==signal);
            }
            else if (editing_==Slot) editing_=kBarCount;
        }
        return true;
    }
    template <unsigned Signal> bool changeGauge(const bmw::Parcel&) {
        if (editing_<kBarCount && !boostDeckSignalHidden(Signal,diesel_,hide_emissions_,n57_index_!=0) && signals_[diesel_][editing_]!=Signal) {
            unsigned char& selected=signals_[diesel_][editing_];
            const unsigned char before=selected;
            selected=Signal;
            if (saveAssignments()) assignmentsChanged();
            else {
                selected=before;
                updateModel(340u+editing_,bmw::BasicString<char>("Could not save gauge selection"));
            }
        }
        return true;
    }
    template <bool Diesel> bool changeProfile(const bmw::Parcel&) {
        if (diesel_!=Diesel) {
            diesel_=Diesel;
            if (saveAssignments()) assignmentsChanged();
            else {
                diesel_=!Diesel;
                updateModel(360u,bmw::BasicString<char>("Could not save engine profile"));
            }
        }
        return true;
    }
    template <bool Hidden> bool hideEmissions(const bmw::Parcel&) {
        const bool before=hide_emissions_;
        hide_emissions_=Hidden;
        if (saveAssignments()) assignmentsChanged();
        else { hide_emissions_=before;updateModel(118u,bmw::BasicString<char>("Could not save emissions setting")); }
        return true;
    }
    void reapCollector() {
        if (collector_ <= 0) return;
        pid_t ended = waitpid(collector_, 0, WNOHANG);
        if (ended == collector_ || (ended < 0 && errno == ECHILD && kill(collector_,0)!=0 && errno==ESRCH)) {
            collector_ = -1;
            stopping_ = false;
        }
    }
    void stopCollector() {
        reapCollector();
        if (collector_ > 0 && !stopping_) {
            kill(collector_, SIGTERM);
            stopping_ = true;
        }
    }
    void shutdown() {
        gauges_visible_=false;
        shown_=focused_=0;
        stopCollector();
        stopTone();
        if (fd_>=0) { close(fd_); fd_=-1; }
    }
    void resetDisplay() {
        memset(&frame_,0,sizeof frame_); memset(labels_,0,sizeof labels_);
        vertical_unit_[0]=0;
        unit_dirty_=true;
        memset(images_,0,sizeof images_);
        memset(dials_,0,sizeof dials_);
        received_=0;
    }
    void refresh();
    void poll();
};

bool BoostDeck::onInit() {
    shutdown();
    resetDisplay();
    loadAssignments();
    struct sockaddr_in a;
    fd_=socket(AF_INET,SOCK_DGRAM,0);
    memset(&a,0,sizeof a); a.sin_family=AF_INET; a.sin_port=htons(BD_UDP_PORT);
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if (fd_>=0 && (fcntl(fd_,F_SETFL,O_NONBLOCK)<0 || fcntl(fd_,F_SETFD,FD_CLOEXEC)<0 ||
        bind(fd_,reinterpret_cast<struct sockaddr*>(&a),sizeof a)<0)) {
        close(fd_); fd_=-1;
    }
    bmw::Delegate<bool,const bmw::Parcel&> action;
    action.m_obj=this;
    typedef bmw::Delegate<bool,const bmw::Map<unsigned,bmw::Variant>&> StateAction;
    StateAction state;state.m_obj=this;
    state.m_exec=&StateAction::execImpl<BoostDeck,&BoostDeck::gaugeActivity<23> >;
    addHandlerWithMap(100u,11u,state);
    state.m_exec=&StateAction::execImpl<BoostDeck,&BoostDeck::gaugeActivity<4> >;
    addHandlerWithMap(100u,1u,state);
    typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
    typedef bool (*Handler)(void*,const bmw::Parcel&);
    static const Handler colorHandlers[] = {
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<0> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<1> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<2> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<3> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<4> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<5> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<6> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<7> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<kMulticolor> >
    };
    typedef char color_handler_count[(sizeof colorHandlers/sizeof colorHandlers[0] ==
                                      sizeof kColorNames/sizeof kColorNames[0]) ? 1 : -1];
    (void)sizeof(color_handler_count);
    for (unsigned i=0;i<sizeof colorHandlers/sizeof colorHandlers[0];++i) {
        action.m_exec=colorHandlers[i];
        addHandler(40u+i,action);
    }
    static const Handler settingHandlers[][10]={
#define C(row, choice) &Action::execImpl<BoostDeck,&BoostDeck::changeSetting<row, choice> >
#define TWO(row) C(row, 0), C(row, 1)
#define SIX(row) TWO(row), C(row, 2), C(row, 3), C(row, 4), C(row, 5)
#define TEN(row) SIX(row), C(row, 6), C(row, 7), C(row, 8), C(row, 9)
        {TWO(BOOST_PRESSURE), C(BOOST_PRESSURE, 2)}, {TWO(BOOST_SPEED)},
        {TWO(BOOST_TEMPERATURE)}, {TWO(BOOST_STYLE)}, {TWO(BOOST_CHIME)},
        {TEN(BOOST_COOLANT_ALERT)}, {TEN(BOOST_OIL_TEMP_ALERT)},
        {SIX(BOOST_OIL_PRESSURE_ALERT)}
#undef TEN
#undef SIX
#undef TWO
#undef C
    };
    for (unsigned row=0;row<BOOST_SETTING_COUNT;++row) {
        for (unsigned choice=0;choice<kBoostChoices[row];++choice) {
            action.m_exec=settingHandlers[row][choice];
            addHandler(2400u+20u*row+choice,action);
        }
    }
    static const Handler railScaleHandlers[]={
        &Action::execImpl<BoostDeck,&BoostDeck::changeRailScale<0> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeRailScale<1> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeRailScale<2> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeRailScale<3> >
    };
    typedef char rail_scale_count[(sizeof railScaleHandlers/sizeof railScaleHandlers[0]==BoostRailScale::COUNT &&
                                   kRailScaleRow==8) ? 1 : -1];
    (void)sizeof(rail_scale_count);
    for (unsigned i=0;i<BoostRailScale::COUNT;++i) {
        action.m_exec=railScaleHandlers[i];
        addHandler(2400u+20u*kRailScaleRow+i,action);
    }
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeProfile<false> >;
    addHandler(502u,action);
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeProfile<true> >;
    addHandler(503u,action);
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::hideEmissions<true> >;
    addHandler(504u,action);
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::hideEmissions<false> >;
    addHandler(505u,action);
    typedef bool (*Editor)(void*,const bmw::Map<unsigned,bmw::Variant>&);
    static const Editor editors[] = {
#define EDIT(slot) &StateAction::execImpl<BoostDeck,&BoostDeck::editGauge<slot> >
        EDIT(0), EDIT(1), EDIT(2), EDIT(3), EDIT(4), EDIT(5),
        EDIT(6), EDIT(7), EDIT(8)
#undef EDIT
    };
    for (unsigned i=0;i<kBarCount;++i) {
        state.m_exec=editors[i];
        addHandlerWithMap(510u+i,11u,state);
    }
    static const Handler changes[] = {
#define SIGNAL(id, name, lo, hi, unit, wide, narrow) &Action::execImpl<BoostDeck,&BoostDeck::changeGauge<BD_##id> >,
#include "boost_signals.def"
#undef SIGNAL
    };
    for (unsigned i=0;i<BD_COUNT;++i) {
        action.m_exec=changes[i];
        addHandler(600u+i,action);
    }
    preferences_.load(kPreferences, sizeof kColorNames / sizeof kColorNames[0]);
    rail_scale_.load(kRailScalePreferences);
    updateSettings();
    updateAssignments();
    if (fd_>=0) {
        ++poll_generation_;
        postPoll();
    }
    return true;
}

template <unsigned Key> bool BoostDeck::gaugeActivity(const bmw::Map<unsigned,bmw::Variant>& event) {
    const bool visible=hmiStateActivity(shown_,focused_,1u,Key,event);
    if (gauges_visible_==visible) return true;
    gauges_visible_=visible;
    if (visible) {
        retry_at_=0;
        startVisibleCollector();
    } else {
        stopCollector();
        stopTone();
        received_=0;
        memset(&frame_,0,sizeof frame_);
        refresh();
    }
    return true;
}

void BoostDeck::startVisibleCollector() {
    if (!gauges_visible_ || collector_>0 || fd_<0 ||
        bd_mono_ms()<retry_at_) return;
    const DiagnosticGateway gateway("/var/opt/hmi/data/ID5/Plugins/" "BoostDeck" "/gateway-ip.txt");
    bd_mask mask=boostDeckSelectionMask(signals_[diesel_],diesel_,hide_emissions_,n57_index_!=0);
    for (unsigned i=0;i<3;++i)
        if (preferences_.choice[BOOST_COOLANT_ALERT+i] &&
            !boostDeckSignalHidden(kAlertSignals[i],diesel_,hide_emissions_,n57_index_!=0)) mask |= BD_BIT(kAlertSignals[i]);
    if (preferences_.choice[BOOST_OIL_PRESSURE_ALERT] && (!diesel_ || !n57_index_)) mask |= BD_BIT(BD_RPM);
    if (!mask) return;
    char selected[24],pin[9];
    snprintf(selected,sizeof selected,"%llu",static_cast<unsigned long long>(mask));
    const char *args[]={kCollector,"--host",gateway.host(),"--session","--signals",selected,
                       0,0,0,0,0,0};
    unsigned next=6;
    if (mask & BD_BIT(BD_ETHANOL)) args[next++]="--ethanol";
    if (diesel_) args[next++]="--diesel";
    if (diesel_ && n57_index_) {
        snprintf(pin,sizeof pin,"%06X",n57_index_);
        args[next++]="--n57-sgbd";args[next++]=pin;
    }
    collector_=launch(args,-1);
    retry_at_=bd_mono_ms()+2000;
}

void BoostDeck::updateSettings() {
    char label[80];
    snprintf(label,sizeof label,"Gauge color: %s",kColorNames[preferences_.color]);
    updateModel(110u,bmw::BasicString<char>(label));
    for (unsigned color=0;color<sizeof kColorNames / sizeof kColorNames[0];++color)
        updateModel(5200u+color,preferences_.color==color);
    for (unsigned row=0;row<BOOST_SETTING_COUNT;++row) {
        char value[32];
        settingValue(preferences_,row,preferences_.choice[row],value,sizeof value);
        snprintf(label,sizeof label,"%s: %s",kSettingLabels[row],value);
        updateModel(120u+row,bmw::BasicString<char>(label));
        for (unsigned choice=0;choice<kBoostChoices[row];++choice) {
            settingValue(preferences_,row,choice,value,sizeof value);
            updateModel(700u+row*20u+choice,bmw::BasicString<char>(value));
            updateModel(5000u+row*20u+choice,preferences_.choice[row]==choice);
        }
    }
    const unsigned units=preferences_.units(BD_RAIL);
    for (unsigned i=0;i<BoostRailScale::COUNT;++i) {
        BoostRailScale option;
        option.choice=i;
        char value[32];
        snprintf(value,sizeof value,"0 - %.0f %s",BoostRailScale::display(option.maximum(),units),
                 BoostRailScale::unit(units));
        updateModel(700u+20u*kRailScaleRow+i,bmw::BasicString<char>(value));
        updateModel(5000u+20u*kRailScaleRow+i,rail_scale_.choice==i);
        if (rail_scale_.choice==i) {
            snprintf(label,sizeof label,"Diesel rail scale: %s",value);
            updateModel(120u+kRailScaleRow,bmw::BasicString<char>(label));
        }
    }
}

void BoostDeck::reapTone() {
    if (tone_>0) {
        pid_t ended=waitpid(tone_,0,WNOHANG);
        if (ended==tone_ || (ended<0 && errno==ECHILD && kill(tone_,0)!=0 && errno==ESRCH)) tone_=-1;
    }
}

void BoostDeck::stopTone() {
    reapTone();
    if (tone_>0) kill(tone_,SIGTERM);
}

void BoostDeck::updateAlerts(uint64_t now, uint64_t elapsed) {
    unsigned mask=0;
    bool chime=false;
    for (unsigned i=0;i<3;++i) {
        const unsigned option=preferences_.choice[BOOST_COOLANT_ALERT+i];
        const unsigned signal=kAlertSignals[i];
        if (i==2 && received_ && bd_is_fresh(&frame_,BD_RPM,elapsed) && frame_.value[BD_RPM]<600)
            alerts_.reset(i);
        const bool valid=!boostDeckSignalHidden(signal,diesel_,hide_emissions_,n57_index_!=0) &&
            received_ && bd_is_fresh(&frame_,signal,elapsed) &&
            (i!=2 || (bd_is_fresh(&frame_,BD_RPM,elapsed) && frame_.value[BD_RPM]>=600));
        bool entered=false;
        const uint64_t sample=valid && received_>frame_.age_ms[signal] ?
            received_-frame_.age_ms[signal] : 0;
        if (alerts_.evaluate(i,option!=0,sample!=0,frame_.value[signal],
                             option ? boostAlertThreshold(i,option) : 0,sample,entered))
            mask|=1u<<i;
        chime=chime || entered;
    }
    if (mask!=alert_mask_) {
        static const char* const text[]={"Coolant hot", "Oil temp high", "Oil pressure low"};
        static const unsigned priority[]={2,0,1};
        char banner[96]="";
        for (unsigned i=0;i<3;++i) {
            if (!(mask&(1u<<priority[i]))) continue;
            if (*banner) strcat(banner," | ");
            strcat(banner,text[priority[i]]);
        }
        updateModel(119u,bmw::BasicString<char>(banner));
        updateModel(68u,bmw::ImageID(mask ? 56999u : kTransparent));
        alert_mask_=mask;
    }
    if (chime && !preferences_.choice[BOOST_CHIME] && tone_<0 &&
        (!last_tone_ || now-last_tone_>=12000)) {
        const char* args[]={kCollector,"--alert-tone",0};
        tone_=launch(args,-1);
        last_tone_=now;
    }
}

void BoostDeck::postPoll() {
    // The framework copies the Message and Parcel into its own queue. Entries
    // carry only data, so clearing the queue needs no code from this module.
    bmw::Parcel data;
    data.writeUInt32(kPollKey);
    data.writeUInt32(poll_generation_);
    bmw::Message message(kDataUpdateMessage, data);
    post(message, collector_ > 0 ? kActivePollIntervalMs : kIdlePollIntervalMs);
}

bool BoostDeck::invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
    if (key!=kPollKey) return bmw::PluginApp::invokeDataUpdate(key,data);
    unsigned generation;
    if (!data.readUInt32(generation)) return false;
    if (fd_<0 || generation!=poll_generation_) return true;
    poll();
    if (fd_>=0 && generation==poll_generation_) postPoll();
    return true;
}

void BoostDeck::poll() {
    reapCollector();
    reapTone();
    startVisibleCollector();
    // A bounded non-blocking drain: at most 8 * 512 bytes per UI tick.
    char data[BD_MAX_PACKET];
    for (unsigned n=0;fd_>=0 && n<8;++n) {
        ssize_t k=recv(fd_,data,sizeof data,0);
        if (k<=0) break;
        bd_frame next;
        if (gauges_visible_ && !stopping_ && bd_parse(data,static_cast<size_t>(k),&next) &&
            next.sent_ms && next.sent_ms<=bd_mono_ms() &&
            (!received_ || next.sent_ms>=received_)) {
            frame_=next; received_=next.sent_ms; // queued UDP does not reset sample age
        }
    }
    refresh();
}

void BoostDeck::loadAssignments() {
    n57_index_=n57_index_file(ID6_N57_SETTINGS);
    loadBoostDeckAssignments(kAssignments,signals_,diesel_,hide_emissions_,n57_index_!=0);
}

void BoostDeck::updateAssignments() {
    for (unsigned i=0;i<kBarCount;++i) {
        char name[128];
        snprintf(name,sizeof name,"Gauge %u: %s",i+1,bd_name(signals_[diesel_][i]));
        updateModel(340u+i,bmw::BasicString<char>(name));
        const unsigned signal=signals_[diesel_][i];
        const char* caption=kGaugeCaptions[signal][i==kVerticalSlot];
        char railCaption[64];
        if (diesel_ && signal==BD_RAIL) {
            const unsigned units=preferences_.units(signal);
            const double maximum=BoostRailScale::display(rail_scale_.maximum(),units);
            snprintf(railCaption,sizeof railCaption,i==kVerticalSlot ? "Rail\n0-%.0f" : "Rail 0-%.0f",maximum);
            caption=railCaption;
        }
        updateModel(370u+i,bmw::BasicString<char>(caption));
        const unsigned parts[]={260u+i,310u+i,280u+i,290u+i};
        bmw::Parcel visible(!boostDeckSignalHidden(signal,diesel_,hide_emissions_,n57_index_!=0));
        for (unsigned part=0;part<4;++part) setProperty(parts[part],3u,visible);
        if (i==kVerticalSlot) setProperty(319u,3u,visible);
        for (unsigned candidate=0;candidate<BD_COUNT;++candidate) {
            bmw::Parcel choice(!boostDeckSignalHidden(candidate,diesel_,hide_emissions_,n57_index_!=0));
            setProperty(3500u+i*64u+candidate,3u,choice);
        }
    }
    updateModel(360u,bmw::BasicString<char>(diesel_ ? (n57_index_ ? "Engine profile: N57" : "Engine profile: Diesel") : "Engine profile: Gasoline"));
    updateModel(5210u,!diesel_);
    updateModel(5211u,diesel_);
    updateModel(5212u,hide_emissions_);
    updateModel(5213u,!hide_emissions_);
    refresh();
}

bool BoostDeck::saveAssignments() {
    return saveBoostDeckAssignments(kAssignments,signals_,diesel_,hide_emissions_);
}

void BoostDeck::restartCollector() {
    stopCollector();
    received_=0;
    memset(&frame_,0,sizeof frame_);
    retry_at_=0;
}

void BoostDeck::assignmentsChanged() {
    restartCollector();
    for (unsigned i=0;i<3;++i) alerts_.reset(i);
    updateAssignments();
}

void BoostDeck::refresh() {
    const uint64_t now=bd_mono_ms();
    const uint64_t elapsed=received_ ? now-received_ : static_cast<uint64_t>(BD_STALE_MS);
    updateAlerts(now,elapsed);
    const bool circular=preferences_.choice[BOOST_STYLE]!=0;
    char vertical_unit[32]="";
    for (unsigned i=0;i<kBarCount;++i) {
        const unsigned signal=signals_[diesel_][i];
        char label[128];
        const bool vertical=i==kVerticalSlot;
        const unsigned steps=vertical ? kVerticalSteps : kDialSteps;
        unsigned image=circular ? kTransparent : vertical ? kVerticalFirst : kBarFirst;
        unsigned dial=circular ? kDialFirst : kTransparent;
        if (!boostDeckSignalHidden(signal,diesel_,hide_emissions_,n57_index_!=0) && received_ && bd_is_fresh(&frame_,signal,elapsed)) {
            const double value=frame_.value[signal];
            const unsigned units=preferences_.units(signal);
            const bool rail_si=diesel_ && signal==BD_RAIL && units==2;
            const char *unit=rail_si ? BoostRailScale::unit(units) : bd_unit_selected(signal,units);
            double reading=rail_si ? BoostRailScale::display(value,units) : bd_display_selected(signal,value,units);
            if (signal==BD_BOOST && fabs(reading)<0.05) reading=0;
            const int decimals=rail_si ? 1 : signal==BD_BATTERY || signal==BD_LAMBDA_TARGET ? 2 :
                signal==BD_RPM || signal==BD_RAIL || signal==BD_ETHANOL ? 0 : 1;
            if (vertical) {
                snprintf(label,sizeof label,"%.*f",decimals,reading);
                snprintf(vertical_unit,sizeof vertical_unit,"%s",unit);
            } else {
                snprintf(label,sizeof label,"%.*f%s%s",decimals,reading,
                         *unit && *unit!='%' ? " " : "",unit);
            }
            const unsigned color=preferences_.color;
            const unsigned palette=color==kMulticolor ? kMulticolor+(i%8) : color;
            const int fill=rail_scale_.bar(signal,value,diesel_);
            if (circular) {
                const unsigned step=(fill*kCircularSteps+500)/1000;
                dial+=1+palette*(kCircularSteps+1)+step;
            } else {
                const unsigned step=(fill*steps+500)/1000;
                if (step) image+=(vertical ? color : palette)*steps+step;
            }
        } else {
            strcpy(label,"--");
        }
        if (image!=images_[i]) {
            updateModel(320u+i,bmw::ImageID(image));
            images_[i]=image;
        }
        if (dial!=dials_[i]) {
            updateModel(330u+i,bmw::ImageID(dial));
            dials_[i]=dial;
        }
        if (strcmp(label,labels_[i])) {
            updateModel(300u+i,bmw::BasicString<char>(label));
            strcpy(labels_[i],label);
        }
    }
    if (unit_dirty_ || strcmp(vertical_unit,vertical_unit_)) {
        updateModel(380u,bmw::BasicString<char>(vertical_unit));
        strcpy(vertical_unit_,vertical_unit);
        unit_dirty_=false;
    }
}

extern "C" {
PluginAppInfo pluginAppInfo = {1, "BoostDeck", "owner-development", 1,
                              create_app<BoostDeck>, destroy_app};
}
