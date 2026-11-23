// Native NBT EVO RA plugin. Models are touched only on the HMI dispatcher.
// Network diagnostics run in the separate collector process, never in HmiMain3D.
#include "plugin_factory.h"
#include "gauge_core.h"
#include "dial_scales.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include "boost_preferences.h"
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
        {"psi","bar","kPa"}, {"mph","km/h",0}, {"\260F","\260C",0},
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
    BoostPreferences preferences_;
    BoostAlerts alerts_;
    pid_t tone_;
    uint64_t last_tone_;
    unsigned alert_mask_;
    uint64_t retry_at_;
    unsigned char signals_[2][kBarCount];
    bool diesel_;
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
    unsigned images_[kBarCount], dials_[kBarCount];
    void postPoll();
public:
    BoostDeck(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned maj, unsigned mn,
              bmw::RHMIInterfaceVersion rhmi, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : bmw::PluginApp(d,name,maj,mn,rhmi,cfg,res), fd_(-1), poll_generation_(0),
          received_(0), collector_(-1), stopping_(false), gauges_visible_(false),
          tone_(-1), last_tone_(0), alert_mask_(0),
          retry_at_(0), diesel_(false), editing_(kBarCount) {
        resetDisplay();
    }
    virtual ~BoostDeck() { shutdown(); }
    virtual bool onInit();
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data);
    bool gaugeVisibility(const bmw::Parcel&);
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
        refresh();
        return true;
    }
    template <unsigned Slot> bool editGauge(const bmw::Parcel& event) {
        bool show;
        if (event.readBool(show)) {
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
        if (editing_<kBarCount && signals_[diesel_][editing_]!=Signal) {
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
    void reapCollector() {
        if (collector_ <= 0) return;
        pid_t ended = waitpid(collector_, 0, WNOHANG);
        if (ended == collector_ || (ended < 0 && errno == ECHILD)) {
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
        stopCollector();
        stopTone();
        if (fd_>=0) { close(fd_); fd_=-1; }
    }
    void resetDisplay() {
        memset(&frame_,0,sizeof frame_); memset(labels_,0,sizeof labels_);
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
    // BMW sends event 11 with a bool when this HMI state becomes visible or hidden.
    action.m_exec=&bmw::Delegate<bool,const bmw::Parcel&>::execImpl<BoostDeck,&BoostDeck::gaugeVisibility>;
    addHandler(100u,11u,action);
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
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeProfile<false> >;
    addHandler(502u,action);
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeProfile<true> >;
    addHandler(503u,action);
    static const Handler editors[] = {
#define EDIT(slot) &Action::execImpl<BoostDeck,&BoostDeck::editGauge<slot> >
        EDIT(0), EDIT(1), EDIT(2), EDIT(3), EDIT(4), EDIT(5),
        EDIT(6), EDIT(7), EDIT(8)
#undef EDIT
    };
    for (unsigned i=0;i<kBarCount;++i) {
        action.m_exec=editors[i];
        addHandler(510u+i,11u,action);
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
    updateSettings();
    updateAssignments();
    if (fd_>=0) {
        ++poll_generation_;
        postPoll();
    }
    return true;
}

bool BoostDeck::gaugeVisibility(const bmw::Parcel& event) {
    bool visible;
    if (!event.readBool(visible) || gauges_visible_==visible) return true;
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
    unsigned mask=0;
    for (unsigned i=0;i<kBarCount;++i) mask |= 1u << signals_[diesel_][i];
    for (unsigned i=0;i<3;++i)
        if (preferences_.choice[BOOST_COOLANT_ALERT+i]) mask |= 1u<<kAlertSignals[i];
    if (preferences_.choice[BOOST_OIL_PRESSURE_ALERT]) mask |= 1u<<BD_RPM;
    char selected[16];
    snprintf(selected,sizeof selected,"%u",mask);
    const char *args[]={kCollector,"--host",gateway.host(),"--session","--signals",selected,
                       0,0,0};
    unsigned next=6;
    if (mask & (1u<<BD_ETHANOL)) args[next++]="--ethanol";
    if (diesel_) args[next]="--diesel";
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
}

void BoostDeck::reapTone() {
    if (tone_>0) {
        pid_t ended=waitpid(tone_,0,WNOHANG);
        if (ended==tone_ || (ended<0 && errno==ECHILD)) tone_=-1;
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
        const bool valid=received_ && bd_is_fresh(&frame_,signal,elapsed) &&
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
    static const unsigned char dieselDefaults[kBarCount]={
        BD_BOOST, BD_LOAD, BD_RPM, BD_IAT, BD_TRANS, BD_RAIL,
        BD_MAF, BD_COOLANT, BD_MAP
    };
    diesel_=false;
    for (unsigned i=0;i<kBarCount;++i) signals_[0][i]=static_cast<unsigned char>(i);
    memcpy(signals_[1],dieselDefaults,sizeof dieselDefaults);
    FILE* file=fopen(kAssignments,"rb");
    if (!file) return;
    const unsigned previousSlots=11;
    unsigned char data[3+2*previousSlots];
    const size_t length=fread(data,1,sizeof data,file);
    const bool complete=fgetc(file)==EOF && !ferror(file);
    fclose(file);
    if (!complete || length<3 || data[0]!='B' || data[2]>1) return;
    unsigned stride=0;
    if (data[1]==2 && length==3+sizeof signals_) stride=kBarCount;
    if (data[1]==1 && length==3+2*previousSlots) stride=previousSlots;
    if (!stride) return;
    for (unsigned profile=0;profile<2;++profile)
        for (unsigned i=0;i<kBarCount;++i)
            if (data[3+profile*stride+i]>=BD_COUNT) return;
    diesel_=data[2]!=0;
    for (unsigned profile=0;profile<2;++profile)
        memcpy(signals_[profile],data+3+profile*stride,kBarCount);
}

void BoostDeck::updateAssignments() {
    for (unsigned i=0;i<kBarCount;++i) {
        char name[64];
        snprintf(name,sizeof name,"Gauge %u: %s",i+1,bd_name(signals_[diesel_][i]));
        updateModel(340u+i,bmw::BasicString<char>(name));
        const unsigned signal=signals_[diesel_][i];
        const char* caption=kGaugeCaptions[signal][i==kVerticalSlot];
        updateModel(370u+i,bmw::BasicString<char>(caption));
    }
    updateModel(360u,bmw::BasicString<char>(diesel_ ? "Engine profile: Diesel" : "Engine profile: Gasoline"));
    updateModel(5210u,!diesel_);
    updateModel(5211u,diesel_);
    refresh();
}

bool BoostDeck::saveAssignments() {
    unsigned char data[3+sizeof signals_]={'B',2,static_cast<unsigned char>(diesel_)};
    memcpy(data+3,signals_,sizeof signals_);
    return savePersistentBytes(kAssignments,data,sizeof data);
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
    for (unsigned i=0;i<kBarCount;++i) {
        const unsigned signal=signals_[diesel_][i];
        char label[128];
        const bool vertical=i==kVerticalSlot;
        const unsigned steps=vertical ? kVerticalSteps : kDialSteps;
        unsigned image=circular ? kTransparent : vertical ? kVerticalFirst : kBarFirst;
        unsigned dial=circular ? kDialFirst : kTransparent;
        if (received_ && bd_is_fresh(&frame_,signal,elapsed)) {
            const double value=frame_.value[signal];
            const unsigned units=preferences_.units(signal);
            const char *unit=bd_unit_selected(signal,units);
            double reading=bd_display_selected(signal,value,units);
            if (signal==BD_BOOST && fabs(reading)<0.05) reading=0;
            const int decimals=signal==BD_BATTERY || signal==BD_LAMBDA_TARGET ? 2 :
                signal==BD_RPM || signal==BD_RAIL || signal==BD_ETHANOL ? 0 : 1;
            snprintf(label,sizeof label,"%.*f%s%s",decimals,reading,
                     *unit && *unit!='%' ? " " : "",unit);
            const unsigned color=preferences_.color;
            const unsigned palette=color==kMulticolor ? kMulticolor+(i%8) : color;
            const int fill=bd_bar(signal,value);
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
}

extern "C" {
PluginAppInfo pluginAppInfo = {1, "BoostDeck", "owner-development", 1,
                              create_app<BoostDeck>, destroy_app};
}
