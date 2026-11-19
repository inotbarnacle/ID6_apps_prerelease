// LaunchDeck: a parked-start 0-60 mph timer. Read-only: it reuses ShiftDeck's speed worker,
// which only polls OBD PIDs 0x0C and 0x0D from the DME, on its own UDP port.
#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include "drive_frame.h"
#include "gauge_core.h"
#include "launch_assets.h"
#include "launch_records.h"
#include "launch_timer.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>

namespace {
const unsigned kLaunchPoll = 0x4C4E4348u;
const unsigned kLaunchUdpPort = 42871u;
const char kLaunchWorker[] = "/var/opt/hmi/data/ID5/Plugins/LaunchDeck/shift-deck-qnx";
const char kLaunchGateway[] = "/var/opt/hmi/data/ID5/Plugins/LaunchDeck/gateway-ip.txt";
const char kLaunchRecords[] = "/var/opt/hmi/data/ID5/LaunchDeck.records";
const char kGlyphs[] = "0123456789. ";
const unsigned kBlank = 11u;

unsigned glyphIndex(char c) {
    const char* found = strchr(kGlyphs, c);
    return found && *found ? static_cast<unsigned>(found - kGlyphs) : kBlank;
}

int mphFromKph(int kph) { return kph < 0 ? -1 : (kph * 621371 + 500000) / 1000000; }
}

class LaunchDeck : public bmw::PluginApp {
    int socket_;
    pid_t child_;
    unsigned generation_;
    bool visible_;
    uint64_t retry_at_;
    drive_frame frame_;
    launch_timer timer_;
    LaunchRecords records_;
    bool new_best_;
    unsigned shown_status_, shown_bar_;
    char shown_big_[8], shown_speed_[8], shown_split_[8], shown_last_[8], shown_best_[8];
    bool shown_gold_;

    void reap() {
        if (child_ <= 0) return;
        pid_t ended = waitpid(child_, 0, WNOHANG);
        if (ended == child_ || (ended < 0 && errno == ECHILD)) child_ = -1;
    }
    void stopChild() {
        reap();
        if (child_ > 0) kill(child_, SIGTERM);
    }
    void shutdown() {
        visible_ = false;
        ++generation_;
        stopChild();
        if (socket_ >= 0) { close(socket_); socket_ = -1; }
    }
    void postPoll() {
        bmw::Parcel data;
        data.writeUInt32(kLaunchPoll);
        data.writeUInt32(generation_);
        post(bmw::Message(0x40000003u, data), visible_ ? 16u : 150u);
    }
    void forgetShown() {
        shown_status_ = shown_bar_ = static_cast<unsigned>(-1);
        shown_gold_ = false;
        shown_big_[0] = shown_speed_[0] = shown_split_[0] = shown_last_[0] = shown_best_[0] = '\x7f';
    }

    // Right-aligned glyph row, redrawn only when the text or colour changes.
    void glyphs(unsigned model, unsigned width, unsigned base, const char* text, char* shown, bool force) {
        if (!force && !strcmp(text, shown)) return;
        unsigned length = static_cast<unsigned>(strlen(text));
        unsigned pad = length < width ? width - length : 0;
        for (unsigned i = 0; i < width; ++i)
            updateModel(model + i, bmw::ImageID(base + (i < pad ? kBlank : glyphIndex(text[i - pad]))));
        snprintf(shown, 8, "%s", text);
    }
    void timeText(uint32_t ms, char* out) {
        if (ms) lt_format_seconds(ms, out, 8);
        else strcpy(out, "");
    }

    unsigned statusFor(bool live) const {
        switch (timer_.state) {
        case LT_ARMED: return kStatusReady;
        case LT_RUNNING: return kStatusRunning;
        case LT_DONE: return new_best_ ? kStatusBest : kStatusDone;
        case LT_ABORTED: return kStatusAborted;
        default: return live ? kStatusIdle : kStatusNodata;
        }
    }

    void draw(uint64_t now) {
        bool live = visible_ && frame_.sampled_ms && frame_.sampled_ms <= now &&
                    now - frame_.sampled_ms < DRIVE_STALE_MS;
        int kph = live ? frame_.kph : -1;

        unsigned status = statusFor(live);
        if (status != shown_status_) {
            updateModel(kModelStatus, bmw::ImageID(kLaunchStatusBase + status));
            shown_status_ = status;
        }

        int mph = mphFromKph(kph);
        unsigned bar = 0;
        if (timer_.state == LT_DONE) bar = kLaunchBarSteps;
        else if (mph > 0) bar = mph >= 60 ? kLaunchBarSteps : static_cast<unsigned>(mph) * kLaunchBarSteps / 60u;
        if (bar != shown_bar_) {
            updateModel(kModelBar, bmw::ImageID(kLaunchBarBase + bar));
            shown_bar_ = bar;
        }

        char text[8];
        bool gold = timer_.state == LT_DONE && new_best_;
        lt_format_seconds(lt_elapsed(&timer_, now), text, sizeof text);
        glyphs(kModelBig, kLaunchBigChars, gold ? kLaunchBigGoldBase : kLaunchBigBase, text, shown_big_,
               gold != shown_gold_);
        shown_gold_ = gold;

        if (mph < 0) strcpy(text, "");
        else snprintf(text, sizeof text, "%d", mph > 999 ? 999 : mph);
        glyphs(kModelSpeed, kLaunchSpeedChars, kLaunchSmallBase, text, shown_speed_, false);
        timeText(timer_.split30_ms, text);
        glyphs(kModelSplit, kLaunchTimeChars, kLaunchSmallBase, text, shown_split_, false);
        timeText(records_.last_ms, text);
        glyphs(kModelLast, kLaunchTimeChars, kLaunchSmallBase, text, shown_last_, false);
        timeText(records_.best_ms, text);
        glyphs(kModelBest, kLaunchTimeChars, kLaunchSmallBase, text, shown_best_, false);
    }

    void feed(uint64_t now) {
        bool live = frame_.sampled_ms && frame_.sampled_ms <= now && now - frame_.sampled_ms < DRIVE_STALE_MS;
        int event = lt_update(&timer_, live ? frame_.sampled_ms : now, live ? frame_.kph : -1);
        if (event == LT_EV_STARTED) new_best_ = false;
        if (event != LT_EV_FINISHED) return;
        new_best_ = lt_is_better(timer_.result_ms, records_.best_ms);
        records_ = withFinishedRun(records_, timer_.result_ms);
        saveLaunchRecords(kLaunchRecords, records_);
    }

    bool visibility(const bmw::Parcel& event) {
        bool show;
        if (!event.readBool(show) || show == visible_) return true;
        visible_ = show;
        memset(&frame_, 0, sizeof frame_);
        lt_init(&timer_);
        new_best_ = false;
        if (show) retry_at_ = 0;
        else stopChild();
        draw(bd_mono_ms());
        return true;
    }
    bool resetBest(const bmw::Parcel&) {
        LaunchRecords cleared = {0, records_.last_ms};
        records_ = cleared;
        new_best_ = false;
        saveLaunchRecords(kLaunchRecords, records_);
        draw(bd_mono_ms());
        return true;
    }

public:
    LaunchDeck(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned maj, unsigned min,
               bmw::RHMIInterfaceVersion version, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : PluginApp(d, name, maj, min, version, cfg, res), socket_(-1), child_(-1), generation_(0),
          visible_(false), retry_at_(0), new_best_(false) {
        memset(&frame_, 0, sizeof frame_);
        lt_init(&timer_);
        records_.best_ms = records_.last_ms = 0;
        forgetShown();
    }
    virtual ~LaunchDeck() { shutdown(); }
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool onInit() {
        shutdown();
        memset(&frame_, 0, sizeof frame_);
        lt_init(&timer_);
        forgetShown();
        records_ = loadLaunchRecords(kLaunchRecords);
        socket_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_ >= 0) {
            struct sockaddr_in address;
            memset(&address, 0, sizeof address);
            address.sin_family = AF_INET;
            address.sin_port = htons(kLaunchUdpPort);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (fcntl(socket_, F_SETFL, O_NONBLOCK) < 0 || fcntl(socket_, F_SETFD, FD_CLOEXEC) < 0 ||
                bind(socket_, reinterpret_cast<struct sockaddr*>(&address), sizeof address) < 0) {
                close(socket_);
                socket_ = -1;
            }
        }
        typedef bmw::Delegate<bool, const bmw::Parcel&> Action;
        Action action;
        action.m_obj = this;
        action.m_exec = &Action::execImpl<LaunchDeck, &LaunchDeck::visibility>;
        addHandler(100u, 11u, action);
        action.m_exec = &Action::execImpl<LaunchDeck, &LaunchDeck::resetBest>;
        addHandler(41u, action);
        draw(bd_mono_ms());
        if (socket_ >= 0) postPoll();
        return true;
    }
    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
        if (key != kLaunchPoll) return PluginApp::invokeDataUpdate(key, data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation != generation_ || socket_ < 0) return true;
        reap();
        uint64_t now = bd_mono_ms();
        if (visible_ && child_ <= 0 && now >= retry_at_) {
            const DiagnosticGateway gateway(kLaunchGateway);
            char port[8];
            snprintf(port, sizeof port, "%u", kLaunchUdpPort);
            const char* args[] = {kLaunchWorker, "--host", gateway.host(), "--udp-port", port, 0};
            child_ = launch(args, -1);
            retry_at_ = now + 1000;
        }
        for (unsigned i = 0; i < 8; ++i) {
            drive_frame next;
            ssize_t n = recv(socket_, &next, sizeof next, 0);
            if (n <= 0) break;
            if (n == sizeof next && visible_ && next.sampled_ms >= frame_.sampled_ms &&
                next.sampled_ms <= now && next.rpm >= -1 && next.rpm <= 16383 &&
                next.kph >= -1 && next.kph <= 255) {
                frame_ = next;
                if (visible_) feed(now);
            }
        }
        if (visible_) feed(now);
        draw(now);
        postPoll();
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo = {1, "LaunchDeck", "owner-development", 1,
                               create_app<LaunchDeck>, destroy_app};
}
