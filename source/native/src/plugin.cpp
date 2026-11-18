// Native NBT EVO RA plugin. Models are touched only on the HMI dispatcher.
// Network diagnostics run in the separate collector process, never in HmiMain3D.
#include "gateway_host.h"
#include "plugin_factory.h"
#include "gauge_core.h"
#include "dial_scales.h"
#include "diagnostic_worker.h"
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
const char kGateway[] = GATEWAY_HOST;
const char kCollector[] = "/var/opt/hmi/data/ID5/Plugins/BoostDeck/boostdeck-collector-qnx";
const unsigned kBarCount = sizeof(kDialScales)/sizeof(kDialScales[0]);

/* QNX softfp mangles a double passed through snprintf, so the reading is
   turned into integers first. The name keeps the string from being drawn as
   a text id. A degree sign stays attached to the number. */
static void write_gauge(char *label, size_t n, const char *name, const char *unit,
                        double shown, int decimals, int signed_plus) {
    int whole, frac, neg;
    double mag;
    const char *gap;
    char value[32];
    if (!name) name = "";
    if (!unit || shown!=shown || shown<=-1000 || shown>=100000) {
        snprintf(label, n, "%s   --", name);
        return;
    }
    neg = shown < 0;
    mag = neg ? -shown : shown;
    gap = (!unit[0] || unit[0]=='\260') ? "" : " ";
    if (!decimals) {
        whole = (int)(mag + 0.5);
        if (neg) snprintf(value, sizeof value, "-%d", whole);
        else if (signed_plus) snprintf(value, sizeof value, "+%d", whole);
        else snprintf(value, sizeof value, "%d", whole);
    } else {
        whole = (int)mag;
        frac = (int)((mag - (double)whole) * 10.0 + 0.5);
        if (frac >= 10) { frac = 0; ++whole; }
        if (neg) snprintf(value, sizeof value, "-%d.%d", whole, frac);
        else if (signed_plus) snprintf(value, sizeof value, "+%d.%d", whole, frac);
        else snprintf(value, sizeof value, "%d.%d", whole, frac);
    }
    if (unit[0]) snprintf(label, n, "%s   %s%s%s", name, value, gap, unit);
    else snprintf(label, n, "%s   %s", name, value);
}
#ifdef PERF_LAB
typedef char dial_count_check[kBarCount + 1 == BD_COUNT ? 1 : -1];
const unsigned kEthanolFirst = 56100u;
const unsigned kEthanolSteps = 20u;
#else
typedef char dial_count_check[kBarCount == BD_COUNT ? 1 : -1];
#endif
}

class BoostDeck : public bmw::PluginApp {
    int fd_;
    unsigned poll_generation_;
    bd_frame frame_;
    uint64_t received_;
    pid_t collector_;
    bool stopping_;
    bool gauges_visible_;
    unsigned color_;
#ifdef PERF_LAB
    bool metric_;
#endif
    uint64_t retry_at_;
    void startVisibleCollector();
    void selectColor(unsigned color);
    char labels_[BD_COUNT][128];
    unsigned images_[BD_COUNT];
    void postPoll();
public:
    BoostDeck(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned maj, unsigned mn,
              bmw::RHMIInterfaceVersion rhmi, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : bmw::PluginApp(d,name,maj,mn,rhmi,cfg,res), fd_(-1), poll_generation_(0),
          received_(0), collector_(-1), stopping_(false), gauges_visible_(false),
          color_(0),
#ifdef PERF_LAB
          metric_(false),
#endif
          retry_at_(0) {
        resetDisplay();
    }
    virtual ~BoostDeck() { shutdown(); }
    virtual bool onInit();
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data);
    bool gaugeVisibility(const bmw::Parcel&);
    template <unsigned Color> bool changeColor(const bmw::Parcel&) {
        selectColor(Color);
        return true;
    }
#ifdef PERF_LAB
    template <bool Metric> bool changeUnits(const bmw::Parcel&) {
        metric_=Metric;
        refresh();
        return true;
    }
#endif
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
        if (fd_>=0) { close(fd_); fd_=-1; }
    }
    void resetDisplay() {
        memset(&frame_,0,sizeof frame_); memset(labels_,0,sizeof labels_);
        memset(images_,0,sizeof images_);
        received_=0;
    }
    void refresh();
    void poll();
};

bool BoostDeck::onInit() {
    shutdown();
    resetDisplay();
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
#ifdef PERF_LAB
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeUnits<false> >;
    addHandler(49u,action);
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeUnits<true> >;
    addHandler(59u,action);
#endif
    selectColor(color_);
    bmw::Parcel visible(true);
    for (unsigned i=0;i<kBarCount;++i) setProperty(200u+i,3u,visible);
    refresh();
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
        received_=0;
        memset(&frame_,0,sizeof frame_);
        refresh();
    }
    return true;
}

void BoostDeck::startVisibleCollector() {
    if (!gauges_visible_ || collector_>0 || fd_<0 ||
        bd_mono_ms()<retry_at_) return;
    const char *args[]={kCollector,"--host",kGateway,"--session",
#ifdef PERF_LAB
                       "--ethanol",
#endif
                       0};
    collector_=launch(args,-1);
    retry_at_=bd_mono_ms()+2000;
}

void BoostDeck::selectColor(unsigned color) {
    color_=color;
    char label[48];
    (void)color_;
    snprintf(label,sizeof label,"Heat: green, yellow, red");
    updateModel(110u,bmw::BasicString<char>(label));
    refresh();
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

void BoostDeck::refresh() {
    uint64_t elapsed=received_ ? bd_mono_ms()-received_ : static_cast<uint64_t>(BD_STALE_MS);
    for (unsigned i=0;i<kBarCount;++i) {
        char label[128];
        const DialScale& scale = kDialScales[i];
        unsigned image = kBarFirst;
        if (received_ && bd_is_fresh(&frame_,i,elapsed)) {
            double x=frame_.value[i];
            double reading = bd_display_value(i,x,0);
            if (i==BD_BOOST && fabs(reading)<0.05) reading=0;
#ifdef PERF_LAB
            const char *unit = scale.unit;
            double shown = reading;
            if (i==BD_TIMING) unit = "\260";
            if (metric_) {
                shown = bd_display_value(i,x,1);
                if (i==BD_OIL_TEMP || i==BD_IAT || i==BD_TRANS || i==BD_COOLANT) unit = "\260C";
                else if (i!=BD_TIMING) unit = "bar";
            }
            write_gauge(label, sizeof label, scale.name, unit, shown, i==BD_RAIL ? 0 : 1, i==BD_BOOST);
#else
            const char *unit_space = *scale.unit ? " " : "";
            const char *prefix = frame_.demo && i==BD_BOOST ? "Demo " : "";
            const char *format = i==BD_BOOST ? "%s%s   %+.1f%s%s"
                                : i==BD_RAIL ? "%s%s   %.0f%s%s"
                                : "%s%s   %.1f%s%s";
            snprintf(label,sizeof label,format,prefix,scale.name,reading,unit_space,scale.unit);
#endif
            double fraction=(x-scale.minimum)/(scale.maximum-scale.minimum);
            if (fraction<0) fraction=0;
            if (fraction>1) fraction=1;
            unsigned step=static_cast<unsigned>(fraction*kDialSteps+.5);
            if (!step) step=1;
            image += kHeatPalette*kDialSteps+step;
        } else {
            snprintf(label,sizeof label,"%s   --",scale.name);
        }
        if (image!=images_[i]) {
            updateModel(60u+i,bmw::ImageID(image));
            images_[i]=image;
        }
        if (strcmp(label,labels_[i])) {
            updateModel(50u+i,bmw::BasicString<char>(label));
            strcpy(labels_[i],label);
        }
    }
#ifdef PERF_LAB
    const bool valid = received_ && bd_is_fresh(&frame_,BD_ETHANOL,elapsed) &&
        frame_.value[BD_ETHANOL]>=0 && frame_.value[BD_ETHANOL]<=100;
    char ethanol[128];
    if (valid) {
        int pct = (int)(frame_.value[BD_ETHANOL] + 0.5);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        snprintf(ethanol,sizeof ethanol,"+%d%%",pct);
    }
    else strcpy(ethanol,"--");
    const unsigned step=valid ? static_cast<unsigned>(frame_.value[BD_ETHANOL]*kEthanolSteps/100.+.5) : 0;
    const unsigned ethanolImage=kEthanolFirst+(step ? color_*kEthanolSteps+step : 0);
    if (images_[BD_ETHANOL]!=ethanolImage) {
        updateModel(68u,bmw::ImageID(ethanolImage));
        images_[BD_ETHANOL]=ethanolImage;
    }
    if (strcmp(labels_[BD_ETHANOL],ethanol)) {
        updateModel(58u,bmw::BasicString<char>(ethanol));
        strcpy(labels_[BD_ETHANOL],ethanol);
    }
#endif
}

extern "C" {
PluginAppInfo pluginAppInfo = {1, "BoostDeck", "owner-development", 1,
                              create_app<BoostDeck>, destroy_app};
}
