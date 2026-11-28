#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include "drive_frame.h"
#include "gauge_core.h"
#include "gauge_preferences.h"
#include "shift_assets.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>

namespace {
const unsigned kShiftPoll=0x53484654u;
const char kShiftWorker[]="/var/opt/hmi/data/ID5/Plugins/ShiftDeck/shift-deck-qnx";
const char kPreferences[]="/var/opt/hmi/data/ID5/ShiftDeck.v2.prefs";
const char kLegacyPreferences[]="/var/opt/hmi/data/ID5/ShiftDeck.prefs";
const unsigned kEaseMs=50;
}

class ShiftDeck : public bmw::PluginApp {
    int socket_;
    pid_t child_;
    unsigned generation_;
    bool visible_;
    unsigned target_;
    uint64_t retry_at_;
    drive_frame frame_;
    unsigned bar_, lights_;
    int rpm_,kph_;
    bool shift_;
    bool metric_;
    double rpm_from_;
    uint64_t rpm_started_;
    double animatedRpm(uint64_t now) const {
        if (!rpm_started_ || now-rpm_started_>=kEaseMs) return frame_.rpm;
        const double remaining=1.-static_cast<double>(now-rpm_started_)/kEaseMs;
        return frame_.rpm+(rpm_from_-frame_.rpm)*remaining*remaining;
    }

    void label(unsigned id,const char* value) { updateModel(id,bmw::BasicString<char>(value)); }
    void reap() {
        if (child_<=0) return;
        pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD)) child_=-1;
    }
    void stopChild() {
        reap();
        if (child_>0) kill(child_,SIGTERM);
    }
    void shutdown() {
        visible_=false;
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
        int kph=live ? frame_.kph : -1;
        int target=static_cast<int>(kShiftTargets[target_]);
        bool shift=rpm>=target;
        const int eased_rpm=rpm>=0 ? static_cast<int>(animatedRpm(now)+.5) : -1;
        unsigned bars=eased_rpm>0 ? (eased_rpm*kSweepSteps+4000)/8000 : 0;
        if (bars>kSweepSteps) bars=kSweepSteps;
        unsigned image=(shift ? kSweepAlert : kSweepBase)+bars;
        if (image!=bar_) {
            updateModel(54u,bmw::ImageID(image));
            bar_=image;
        }
        int approach=rpm-(target-2000);
        unsigned lights=shift ? 13u : approach>0 ? static_cast<unsigned>(approach)*12/2000 : 0u;
        if (lights!=lights_) {
            updateModel(55u,bmw::ImageID(kLightBase+lights));
            lights_=lights;
        }
        char text[48];
        const int displayed_rpm=eased_rpm<0 ? -1 : (eased_rpm+5)/10*10;
        if (displayed_rpm!=rpm_ || shift!=shift_) {
            if (displayed_rpm<0) strcpy(text,"--");
            else snprintf(text,sizeof text,"%d",displayed_rpm>9990 ? 9990 : displayed_rpm);
            digits(60u,4u,text,shift ? kDigitWarn : kDigitBase,true);
            rpm_=displayed_rpm;
        }
        if (kph!=kph_) {
            if (kph<0) strcpy(text,"--");
            else {
            int speed=(kph*621371+500000)/1000000;
            if (metric_) speed=kph;
            snprintf(text,sizeof text,"%d",speed);
        }
            digits(70u,3u,text,kDigitBase,false);
            kph_=kph;
        }
        if (shift!=shift_) {
            updateModel(57u,bmw::ImageID(shift ? kShiftOn : kShiftOff));
            shift_=shift;
        }
    }
    bool visibility(const bmw::Parcel& event) {
        bool show;
        if (!event.readBool(show) || show==visible_) return true;
        visible_=show;
        frame_.sampled_ms=0;
        rpm_started_=0;
        if (show) retry_at_=0;
        else stopChild();
        draw(bd_mono_ms());
        return true;
    }
    template <unsigned Index> bool setTarget(const bmw::Parcel&) {
        target_=Index;
        saveGaugePreferences(kPreferences, target_, metric_);
        showTarget();
        draw(bd_mono_ms());
        return true;
    }
    template <bool Metric> bool setUnits(const bmw::Parcel&) {
        metric_=Metric;
        saveGaugePreferences(kPreferences, target_, metric_);
        kph_=-2;
        updateModel(64u,bmw::ImageID(metric_ ? kSpeedLegendKmh : kSpeedLegendMph));
        draw(bd_mono_ms());
        return true;
    }
    void showTarget() {
        char text[48];
        snprintf(text,sizeof text,"Shift light at %u RPM",kShiftTargets[target_]);
        label(53u,text);
        updateModel(56u,bmw::ImageID(kTargetBase+target_));
        updateModel(68u,bmw::ImageID(kMarkBase+target_));
    }

public:
    ShiftDeck(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned maj,unsigned min,
              bmw::RHMIInterfaceVersion version,const bmw::Bundle& cfg,const bmw::Bundle& res)
        : PluginApp(d,name,maj,min,version,cfg,res),socket_(-1),child_(-1),generation_(0),
          visible_(false),target_(kDefaultTarget),retry_at_(0),bar_(0),lights_(0),
          rpm_(-2),kph_(-2),shift_(false),metric_(false) {
        memset(&frame_,0,sizeof frame_);
        rpm_from_=0;
        rpm_started_=0;
    }
    virtual ~ShiftDeck() { shutdown(); }
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool onInit() {
        shutdown();
        memset(&frame_,0,sizeof frame_);
        bar_=0;
        lights_=static_cast<unsigned>(-1);
        rpm_=kph_=-2;
        shift_=false;
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
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::visibility>;
        addHandler(100u,11u,action);
        typedef bool (*Handler)(void*,const bmw::Parcel&);
#define TARGET(i) &Action::execImpl<ShiftDeck,&ShiftDeck::setTarget<i> >
        const Handler targets[]={
            TARGET(0),TARGET(1),TARGET(2),TARGET(3),TARGET(4),TARGET(5),TARGET(6),
            TARGET(7),TARGET(8),TARGET(9),TARGET(10),TARGET(11),TARGET(12),TARGET(13),
            TARGET(14),TARGET(15),TARGET(16),TARGET(17),TARGET(18),TARGET(19),TARGET(20)
        };
#undef TARGET
        for (unsigned i=0;i<sizeof targets/sizeof targets[0];++i) {
            action.m_exec=targets[i];
            addHandler(400u+i,action);
        }
        if (!loadGaugePreferences(kPreferences, sizeof kShiftTargets / sizeof kShiftTargets[0], target_, metric_)) {
            unsigned legacy;
            static const unsigned converted[]={0,0,3,5,7};
            if (loadGaugePreferences(kLegacyPreferences, sizeof converted / sizeof converted[0], legacy, metric_))
                target_=converted[legacy];
        }
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setUnits<false> >;
        addHandler(46u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setUnits<true> >;
        addHandler(47u,action);
        updateModel(64u,bmw::ImageID(metric_ ? kSpeedLegendKmh : kSpeedLegendMph));
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
            drive_frame next;
            ssize_t n=recv(socket_,&next,sizeof next,0);
            if (n<=0) break;
            if (n==sizeof next && visible_ && next.sampled_ms>=frame_.sampled_ms &&
                next.sampled_ms<=now && next.rpm>=-1 && next.rpm<=16383 &&
                next.kph>=-1 && next.kph<=255) {
                if (next.rpm!=frame_.rpm) {
                    const bool continuous=next.rpm>=0 && frame_.rpm>=0 && frame_.sampled_ms &&
                                          now-frame_.sampled_ms<DRIVE_STALE_MS;
                    rpm_from_=continuous ? animatedRpm(now) : next.rpm;
                    rpm_started_=continuous ? now : 0;
                }
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
