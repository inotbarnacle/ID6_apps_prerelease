#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include "drive_frame.h"
#include "gauge_core.h"
#include "settings.h"
#include "shift_assets.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>

namespace {
const unsigned kShiftPoll=0x53484654u;
const char kShiftWorker[]=ENA_SHIFTDECK_WORKER;
// The screen redraws every 16 ms while visible. The bar eases from the shown revs to each new reading over kEaseMs,
// so it sweeps instead of jumping between the worker's readings (about every 20 ms).
const unsigned kVisiblePollMs=16u, kHiddenPollMs=150u, kEaseMs=50u;
const unsigned kBarFullRpm=8000u;
// The Speed setting: M (MPH, upstream's unit, until one is picked) or K (km/h) on one line.
const char kSpeedFile[]=ENA_DATA_DIR "/shiftdeck-speed";
const char kTargetFile[]=ENA_DATA_DIR "/shift-target";
const char* const kSpeedUnits[]={"MPH","km/h"};
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
    double rpm_from_;      // the revs shown when the latest reading arrived
    uint64_t rpm_started_; // when it arrived; 0 shows the reading as it is

    void label(unsigned id,const char* value) { updateModel(id,bmw::BasicString<char>(value)); }
    double animatedRpm(uint64_t now) const {
        if (!rpm_started_ || now-rpm_started_>=kEaseMs) return frame_.rpm;
        const double remaining=1.-static_cast<double>(now-rpm_started_)/kEaseMs;
        return frame_.rpm+(rpm_from_-frame_.rpm)*remaining*remaining;
    }
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
        post(bmw::Message(0x40000003u,data),visible_ ? kVisiblePollMs : kHiddenPollMs);
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
        int shown=rpm>=0 ? static_cast<int>(animatedRpm(now)+.5) : -1;
        unsigned bars=shown>0 ? (static_cast<unsigned>(shown)*kSweepSteps+kBarFullRpm/2)/kBarFullRpm : 0;
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
        // The digits follow the eased revs too, to the nearest 10 so the last digit doesn't flicker.
        int displayed_rpm=shown<0 ? -1 : (shown+5)/10*10;
        if (displayed_rpm!=rpm_ || shift!=shift_) {
            if (displayed_rpm<0) strcpy(text,"--");
            else snprintf(text,sizeof text,"%d",displayed_rpm>9999 ? 9999 : displayed_rpm);
            digits(60u,4u,text,shift ? kDigitWarn : kDigitBase,true);
            rpm_=displayed_rpm;
        }
        if (kph!=kph_) {
            if (kph<0) strcpy(text,"--");
            else snprintf(text,sizeof text,"%d",metric_ ? kph : (kph*621371+500000)/1000000);
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
        const char preference[]={static_cast<char>('A'+Index),'\n',0};
        save_setting(kTargetFile,preference);
        showTarget();
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
    // The Speed screen's buttons (tools/make_drive_apps.py): MPH is action 46, km/h 47.
    template <bool Metric> bool setSpeedUnit(const bmw::Parcel&) {
        selectSpeedUnit(Metric);
        save_setting(kSpeedFile,Metric ? "K\n" : "M\n");
        return true;
    }
    void selectSpeedUnit(bool metric) {
        char text[48];
        metric_=metric;
        kph_=-2; // redraw the speed in the new unit
        snprintf(text,sizeof text,"Selected: %s",kSpeedUnits[metric ? 1 : 0]);
        label(58u,text);
        updateModel(64u,bmw::ImageID(metric ? kSpeedLegendKmh : kSpeedLegendMph));
        draw(bd_mono_ms());
    }

public:
    ShiftDeck(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned maj,unsigned min,
              bmw::RHMIInterfaceVersion version,const bmw::Bundle& cfg,const bmw::Bundle& res)
        : PluginApp(d,name,maj,min,version,cfg,res),socket_(-1),child_(-1),generation_(0),
          visible_(false),target_(kDefaultTarget),retry_at_(0),bar_(0),lights_(0),
          rpm_(-2),kph_(-2),shift_(false),metric_(false),rpm_from_(0),rpm_started_(0) {
        memset(&frame_,0,sizeof frame_);
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
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setSpeedUnit<false> >;
        addHandler(46u,action);
        action.m_exec=&Action::execImpl<ShiftDeck,&ShiftDeck::setSpeedUnit<true> >;
        addHandler(47u,action);
        rpm_started_=0;
        updateModel(57u,bmw::ImageID(kShiftOff));
        const char saved=load_setting(kTargetFile,static_cast<char>('A'+kDefaultTarget));
        if (saved>='A' && static_cast<unsigned>(saved-'A')<sizeof kShiftTargets/sizeof kShiftTargets[0])
            target_=static_cast<unsigned>(saved-'A');
        showTarget();
        selectSpeedUnit(load_setting(kSpeedFile,'M')=='K');
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
            char host[16];
            if (installed_gateway(host)) {
                const char* args[]={kShiftWorker,"--host",host,0};
                child_=launch(args,-1);
            }
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
                    // Ease only between two live readings; after a gap the bar shows the new one at once.
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
PLUGIN_EXPORT PluginAppInfo pluginAppInfo = {1,"ShiftDeck","owner-development",1,
                            create_app<ShiftDeck>,destroy_app};
}
