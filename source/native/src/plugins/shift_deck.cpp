#include "plugin_factory.h"
#include "hmi_state_activity.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include "drive_frame.h"
#include "gauge_core.h"
#include "gauge_preferences.h"
#include "shift_assets.h"
#include "shift_scale.h"
#include "shift_window.h"
#include "n57.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <errno.h>

namespace {
const unsigned kShiftPoll=0x53484654u;
const char kShiftWorker[]="/var/opt/hmi/data/ID5/Plugins/ShiftDeck/shift-deck-qnx";
const char kPreferences[]="/var/opt/hmi/data/ID5/ShiftDeck.prefs";
const char kN57Preferences[]="/var/opt/hmi/data/ID5/ShiftDeck.n57.prefs";
const char kN57WindowPreferences[]="/var/opt/hmi/data/ID5/ShiftDeck.n57.window.prefs";
#ifdef NATIVE_SHIFT_SMOOTH
const unsigned kSweepSteps=128, kSweepBase=56400, kSweepAlert=56529;
const unsigned kEaseMs=50;
#endif
}

class ShiftDeck : public bmw::PluginApp {
    int socket_;
    pid_t child_;
    unsigned generation_;
    bool visible_;
    unsigned shown_,focused_;
    unsigned target_;
    uint64_t retry_at_;
    drive_sample frame_;
    unsigned bar_, lights_;
    int rpm_,kph_;
    unsigned signal_;
    bool metric_;
    bool n57_;
    ShiftWindow window_;
    const char *preferences() const { return n57_ ? kN57Preferences : kPreferences; }
    void loadPreferences() {
        target_=n57_ ? kDefaultN57Target : kDefaultTarget;
        metric_=false;
        loadGaugePreferences(preferences(),sizeof kShiftTargets/sizeof kShiftTargets[0],target_,metric_);
        if (!shift_target_allowed(target_,n57_)) target_=kDefaultN57Target;
        window_=ShiftWindow();
        if (n57_) window_.load(kN57WindowPreferences);
    }
#ifdef NATIVE_SHIFT_SMOOTH
    double rpm_from_;
    uint64_t rpm_started_;
    double animatedRpm(uint64_t now) const {
        if (!rpm_started_ || now-rpm_started_>=kEaseMs) return frame_.rpm;
        const double remaining=1.-static_cast<double>(now-rpm_started_)/kEaseMs;
        return frame_.rpm+(rpm_from_-frame_.rpm)*remaining*remaining;
    }
#endif

    void label(unsigned id,const char* value) { updateModel(id,bmw::BasicString<char>(value)); }
    void reap() {
        if (child_<=0) return;
        pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD && kill(child_,0)!=0 && errno==ESRCH)) child_=-1;
    }
    void stopChild() {
        reap();
        if (child_>0) kill(child_,SIGTERM);
    }
    void shutdown() {
        visible_=false;
        shown_=focused_=0;
        ++generation_;
        stopChild();
        if (socket_>=0) { close(socket_); socket_=-1; }
    }
    void postPoll() {
        bmw::Parcel data;
        data.writeUInt32(kShiftPoll);
        data.writeUInt32(generation_);
        post(bmw::Message(0x40000003u,data),visible_ ? 16u : 150u);
    }
    void digits(unsigned model, unsigned width, const char* text, unsigned base, bool center) {
        unsigned length=static_cast<unsigned>(strlen(text));
        unsigned offset=center && length<width ? (width-length)/2 : 0;
        for (unsigned i=0;i<width;++i) {
            char c=i>=offset && i-offset<length ? text[i-offset] : ' ';
            updateModel(model+i,bmw::ImageID(base+(c==' ' ? 11u : c=='-' ? 10u : c-'0')));
        }
    }
    void draw(uint64_t now) {
        bool live=visible_ && frame_.sampled_ms && frame_.sampled_ms<=now &&
                  now-frame_.sampled_ms<DRIVE_STALE_MS;
        int rpm=live ? frame_.rpm : -1;
        int speed=live ? drive_display_speed(frame_.mkph,
#ifdef NATIVE_SHIFT_SMOOTH
            metric_
#else
            0
#endif
        ) : -1;
        int target=static_cast<int>(n57_ ? window_.lower : kShiftTargets[target_]);
        unsigned signal=n57_ ? window_.signal(rpm,live) :
            static_cast<unsigned>(rpm>=target ? ShiftWindow::Strong : ShiftWindow::Normal);
        bool warning=signal==ShiftWindow::Warning;
        bool strong=signal==ShiftWindow::Strong;
        int displayed_rpm=rpm;
#ifdef NATIVE_SHIFT_SMOOTH
        if (rpm>=0) displayed_rpm=static_cast<int>(animatedRpm(now)+.5);
        unsigned bars=shift_scale_step(displayed_rpm,n57_,kSweepSteps);
        unsigned image=(warning ? kN57SweepWarning : strong ? kSweepAlert :
                                 n57_ ? kN57SweepNormal : kSweepBase)+bars;
#else
        unsigned bars=shift_scale_step(rpm,n57_,32u);
        unsigned image=(n57_ ? (warning ? kN57BarWarning : strong ? kN57BarWarn : kN57BarBase) :
                               (strong ? kBarWarn : kBarBase))+bars;
#endif
        if (image!=bar_) {
            updateModel(54u,bmw::ImageID(image));
            bar_=image;
        }
        int approach=rpm-(target-2000);
        unsigned lights=n57_ ? (warning ? kN57LightsWarning : kLightBase+(strong ? 13u : 0u)) :
            kLightBase+(strong ? 13u : approach>0 ? static_cast<unsigned>(approach)*12/2000 : 0u);
        if (lights!=lights_) {
            updateModel(55u,bmw::ImageID(lights));
            lights_=lights;
        }
        char text[48];
        if (displayed_rpm!=rpm_ || signal!=signal_) {
            if (displayed_rpm<0) strcpy(text,"--");
            else snprintf(text,sizeof text,"%d",displayed_rpm>9999 ? 9999 : displayed_rpm);
            digits(60u,4u,text,warning ? kN57DigitWarning : strong ? kDigitWarn : kDigitBase,true);
            rpm_=displayed_rpm;
        }
        if (speed!=kph_) {
            if (speed<0) strcpy(text,"--");
            else snprintf(text,sizeof text,"%d",speed);
            digits(70u,3u,text,kDigitBase,false);
            kph_=speed;
        }
        if (signal!=signal_) {
            updateModel(57u,bmw::ImageID(warning ? kN57ShiftWarning : strong ? kShiftOn : kShiftOff));
            signal_=signal;
        }
    }
    template <unsigned Key> bool stateActivity(const bmw::Map<unsigned,bmw::Variant>& event) {
        const bool show=hmiStateActivity(shown_,focused_,1u,Key,event);
        if (show==visible_) return true;
        visible_=show;
        frame_.sampled_ms=0;
#ifdef NATIVE_SHIFT_SMOOTH
        rpm_started_=0;
#endif
        if (show) retry_at_=0;
        else stopChild();
        draw(bd_mono_ms());
        return true;
    }
    template <unsigned Index> bool setTarget(const bmw::Parcel&) {
        if (n57_ || !shift_target_allowed(Index,false)) return true;
        target_=Index;
        saveGaugePreferences(preferences(), target_, metric_);
        showTarget();
        draw(bd_mono_ms());
        return true;
    }
    void showWindow(const char* feedback=0) {
        char text[128];
        snprintf(text,sizeof text,"%u - %u",window_.lower,window_.upper);
        label(80u,text);
        if (feedback) snprintf(text,sizeof text,"%s; unchanged: %u - %u RPM",feedback,window_.lower,window_.upper);
        else snprintf(text,sizeof text,"Warning %u / strong %u RPM",window_.lower,window_.upper);
        label(83u,text);
    }
    bool saveWindow(const ShiftWindow& candidate) {
        if (!candidate.save(kN57WindowPreferences)) {
            showWindow("Save failed");
            return true;
        }
        window_=candidate;
        showTarget();
        draw(bd_mono_ms());
        return true;
    }
    template <int Lower,int Upper> bool adjustWindow(const bmw::Parcel&) {
        if (!n57_) return true;
        ShiftWindow candidate=window_;
        if ((Lower && !candidate.adjustLower(Lower)) || (Upper && !candidate.adjustUpper(Upper))) {
            showWindow("Limit reached");
            return true;
        }
        return saveWindow(candidate);
    }
    bool resetWindow(const bmw::Parcel&) {
        if (!n57_) return true;
        return saveWindow(ShiftWindow());
    }
#ifdef NATIVE_SHIFT_SMOOTH
    template <bool Metric> bool setUnits(const bmw::Parcel&) {
        metric_=Metric;
        saveGaugePreferences(preferences(), target_, metric_);
        kph_=-2;
        updateModel(64u,bmw::ImageID(metric_ ? 56660u : 55916u));
        draw(bd_mono_ms());
        return true;
    }
#endif
    void showTarget() {
        char text[48];
        snprintf(text,sizeof text,"Shift light at %u RPM",kShiftTargets[target_]);
        label(53u,text);
        updateModel(56u,bmw::ImageID(kTargetBase+target_));
        updateModel(66u,bmw::ImageID(n57_ ? kN57Scale : kScale));
        updateModel(68u,bmw::ImageID((n57_ ? kN57MarkBase : kMarkBase)+target_));
        bmw::Parcel gasoline(!n57_);
        for (unsigned i=0;i<8u;++i) setProperty(301u+i,3u,gasoline);
        setProperty(30u,3u,gasoline);
        setProperty(202u,3u,gasoline);
        setProperty(214u,3u,gasoline);
        bmw::Parcel diesel(n57_);
        setProperty(412u,3u,diesel);
        for (unsigned i=250;i<=253;++i) setProperty(i,3u,diesel);
        if (n57_) {
            updateModel(81u,bmw::ImageID(kN57LowerMarker));
            updateModel(82u,bmw::ImageID(kN57UpperMarker));
            bmw::Parcel lower,upper;
            lower.writeUInt32(kN57AxisLeft+(kN57AxisSpan*window_.lower+3000u)/6000u-3u);
            upper.writeUInt32(kN57AxisLeft+(kN57AxisSpan*window_.upper+3000u)/6000u-3u);
            setProperty(250u,20u,lower);
            setProperty(251u,20u,upper);
            showWindow();
        }
    }

public:
    ShiftDeck(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned maj,unsigned min,
              bmw::RHMIInterfaceVersion version,const bmw::Bundle& cfg,const bmw::Bundle& res)
        : PluginApp(d,name,maj,min,version,cfg,res),socket_(-1),child_(-1),generation_(0),
          visible_(false), shown_(0),focused_(0),target_(kDefaultTarget),retry_at_(0),bar_(0),lights_(0),
          rpm_(-2),kph_(-2),signal_(ShiftWindow::Normal),metric_(false),n57_(false) {
        memset(&frame_,0,sizeof frame_);
#ifdef NATIVE_SHIFT_SMOOTH
        rpm_from_=0;
        rpm_started_=0;
#endif
    }
    virtual ~ShiftDeck() { shutdown(); }
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool onInit() {
        shutdown();
        memset(&frame_,0,sizeof frame_);
        bar_=0;
        lights_=static_cast<unsigned>(-1);
        rpm_=kph_=-2;
        signal_=ShiftWindow::Normal;
        socket_=socket(AF_INET,SOCK_DGRAM,0);
        if (socket_>=0) {
            struct sockaddr_in address;
            memset(&address,0,sizeof address);
            address.sin_family=AF_INET;
            address.sin_port=htons(DRIVE_PORT);
            address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            if (fcntl(socket_,F_SETFL,O_NONBLOCK)<0 || fcntl(socket_,F_SETFD,FD_CLOEXEC)<0 ||
                bind(socket_,reinterpret_cast<struct sockaddr*>(&address),sizeof address)<0) {
                close(socket_);
                socket_=-1;
            }
        }
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        Action action;
        action.m_obj=this;
        typedef bmw::Delegate<bool,const bmw::Map<unsigned,bmw::Variant>&> StateAction;
        StateAction state;state.m_obj=this;
        state.m_exec=&StateAction::execImpl<ShiftDeck,&ShiftDeck::stateActivity<23> >;
        addHandlerWithMap(100u,11u,state);
        state.m_exec=&StateAction::execImpl<ShiftDeck,&ShiftDeck::stateActivity<4> >;
        addHandlerWithMap(100u,1u,state);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<0> >;
        addHandler(41u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<1> >;
        addHandler(42u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<2> >;
        addHandler(43u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<3> >;
        addHandler(44u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<4> >;
        addHandler(45u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<5> >;
        addHandler(48u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<6> >;
        addHandler(49u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<7> >;
        addHandler(50u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::adjustWindow<-100,0> >;
        addHandler(121u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::adjustWindow<100,0> >;
        addHandler(122u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::adjustWindow<0,-100> >;
        addHandler(123u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::adjustWindow<0,100> >;
        addHandler(124u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::resetWindow>;
        addHandler(125u,action);
        unsigned index=0;
        n57_=n57_settings(&index)==1;
        loadPreferences();
#ifdef NATIVE_SHIFT_SMOOTH
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setUnits<false> >;
        addHandler(46u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setUnits<true> >;
        addHandler(47u,action);
        updateModel(64u,bmw::ImageID(metric_ ? 56660u : 55916u));
#endif
        updateModel(57u,bmw::ImageID(kShiftOff));
        showTarget();
        draw(bd_mono_ms());
        if (socket_>=0) postPoll();
        return true;
    }
    virtual bool invokeDataUpdate(unsigned key,const bmw::Parcel& data) {
        if (key!=kShiftPoll) return PluginApp::invokeDataUpdate(key,data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation!=generation_ || socket_<0) return true;
        reap();
        uint64_t now=bd_mono_ms();
        if (visible_ && child_<=0 && now>=retry_at_) {
            const DiagnosticGateway gateway("/var/opt/hmi/data/ID5/Plugins/" "ShiftDeck" "/gateway-ip.txt");
            const char* args[]={kShiftWorker,"--host",gateway.host(),0};
            child_=launch(args,-1);
            retry_at_=now+1000;
        }
        for (unsigned i=0;i<8;++i) {
            char packet[sizeof(drive_n57_frame)+1];
            drive_sample next;
            ssize_t n=recv(socket_,packet,sizeof packet,0);
            if (n<=0) break;
            if (visible_ && drive_parse(packet,static_cast<size_t>(n),&next) &&
                next.sampled_ms>=frame_.sampled_ms && next.sampled_ms<=now) {
                if ((next.n57!=0)!=n57_) {
                    n57_=next.n57!=0;
                    loadPreferences();
                    showTarget();
                    kph_=-2;
#ifdef NATIVE_SHIFT_SMOOTH
                    updateModel(64u,bmw::ImageID(metric_ ? 56660u : 55916u));
#endif
                }
#ifdef NATIVE_SHIFT_SMOOTH
                if (next.rpm!=frame_.rpm) {
                    const bool continuous=next.n57==frame_.n57 && next.rpm>=0 && frame_.rpm>=0 && frame_.sampled_ms &&
                                          now-frame_.sampled_ms<DRIVE_STALE_MS;
                    rpm_from_=continuous ? animatedRpm(now) : next.rpm;
                    rpm_started_=continuous ? now : 0;
                }
#endif
                frame_=next;
            }
        }
        draw(now);
        postPoll();
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo={1,"ShiftDeck","owner-development",1,
                            create_app<ShiftDeck>,destroy_app};
}
