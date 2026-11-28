// Bootmod3 Suite (the Bm3Maps plugin, BM3 Maps until 0.1.0): bootmod3 CustomROM controls from iDrive. The bm3-maps
// worker does the diagnostics (src/bm3.h); this plugin starts it and shows the line it prints. Opening the page reads the active map; a tile writes that slot and
// reads it back. OPTIONS has Active Sound and burbles, each on its own screen with its own result line. One worker at
// a time: a press while one runs only says so.
// Independent of and unaffiliated with bootmod3.
#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const unsigned kDataUpdateMessage = 0x40000003u;
const unsigned kMapPoll = 0x424d3350u;  // BM3P, private to this app
const char kMapWorker[] = ENA_BM3MAPS_WORKER;

// The result lines: the main page's, then the option screens' (tools/make_drive_apps.py, BM3_SCREENS).
const unsigned kMapLine = 50u, kSoundLine = 52u, kBurbleLine = 53u;
// The map tiles (tools/make_drive_apps.py, MAP_TILES): none active, then map n active at kTiles + n.
const unsigned kTilesModel = 61u, kTiles = 56010u;

// The active map in a worker's line, exactly "Map N active", or 0.
unsigned active_map(const char* line) {
    unsigned slot;
    int used = 0;
    return sscanf(line, "Map %u active%n", &slot, &used) == 1 && used && !line[used] && slot >= 1 && slot <= 4
               ? slot : 0;
}
}

class Bm3Maps : public bmw::PluginApp {
    pid_t child_;
    int pipe_;
    unsigned generation_;
    unsigned line_;  // where the running worker's result goes
    char result_[128];
    size_t used_;

    void status(const char* value) { status(line_, value); }
    void status(unsigned line, const char* value) { updateModel(line, bmw::BasicString<char>(value)); }
    void reap() {
        if (child_ <= 0) return;
        pid_t ended = waitpid(child_, 0, WNOHANG);
        if (ended == child_ || (ended < 0 && errno == ECHILD)) child_ = -1;
    }
    void stopWorker() {
        ++generation_;
        if (pipe_ >= 0) { close(pipe_); pipe_ = -1; }
        reap();
        if (child_ > 0) kill(child_, SIGTERM);
    }
    void postPoll() {
        bmw::Parcel data;
        data.writeUInt32(kMapPoll);
        data.writeUInt32(generation_);
        post(bmw::Message(kDataUpdateMessage, data), 100u);
    }
    // Starts the worker with these arguments after --host; its line replaces line's text when it ends.
    void run(unsigned line, const char* mode, const char* value, const char* pending) {
        reap();
        if (pipe_ >= 0 || child_ > 0) {
            if (line != line_) status(line, "Busy with another request. Try again in a moment.");
            return;
        }
        ++generation_;  // a late poll from an earlier run can't touch this one
        line_ = line;
        char host[16];
        if (!installed_gateway(host)) {
            status("Gateway not set up. Run setup.sh install again.");
            return;
        }
        int channel[2];
        if (pipe(channel) < 0) { status("Could not start the request"); return; }
        const char* args[] = {kMapWorker, "--host", host, mode, value, 0};
        if (fcntl(channel[0], F_SETFL, O_NONBLOCK) >= 0 && fcntl(channel[0], F_SETFD, FD_CLOEXEC) >= 0 &&
            fcntl(channel[1], F_SETFD, FD_CLOEXEC) >= 0)
            child_ = launch(args, channel[1]);
        close(channel[1]);
        if (child_ < 0) {
            close(channel[0]);
            status("Could not start the request");
            return;
        }
        pipe_ = channel[0];
        used_ = 0;
        status(pending);
        postPoll();
    }
    template <unsigned Slot> bool selectMap(const bmw::Parcel&) {
        static const char slot[] = {static_cast<char>('0' + Slot), 0};
        char message[32];
        snprintf(message, sizeof message, "Switching to map %u...", Slot);
        run(kMapLine, "--map", slot, message);
        return true;
    }
    template <unsigned On> bool selectSound(const bmw::Parcel&) {
        static const char on[] = {static_cast<char>('0' + On), 0};
        run(kSoundLine, "--sound", on, "Sending the Active Sound request...");
        return true;
    }
    // The worker's --burbles 0..3: off, then aggression 4, 8 and 12.
    template <unsigned Preset> bool selectBurbles(const bmw::Parcel&) {
        static const char preset[] = {static_cast<char>('0' + Preset), 0};
        run(kBurbleLine, "--burbles", preset, "Writing the burble setting...");
        return true;
    }
    bool pageVisibility(const bmw::Parcel& event) {
        bool visible;
        if (!event.readBool(visible) || !visible) return true;
        reap();
        // Back from an option screen while its request runs: that request's line has the news, not this page.
        if (pipe_ < 0 && child_ <= 0) run(kMapLine, "--status", 0, "Reading the active map...");
        return true;
    }

public:
    Bm3Maps(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned maj, unsigned min,
            bmw::RHMIInterfaceVersion version, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : PluginApp(d, name, maj, min, version, cfg, res), child_(-1), pipe_(-1), generation_(0), line_(kMapLine),
          used_(0) {}
    virtual ~Bm3Maps() { stopWorker(); }
    virtual bool onFinalize() {
        stopWorker();
        return true;
    }
    virtual bool onInit() {
        stopWorker();
        typedef bmw::Delegate<bool, const bmw::Parcel&> Action;
        Action action;
        action.m_obj = this;
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectMap<1> >;
        addHandler(21u, action);
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectMap<2> >;
        addHandler(22u, action);
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectMap<3> >;
        addHandler(23u, action);
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectMap<4> >;
        addHandler(24u, action);
        // The option screens' buttons, in tools/make_drive_apps.py's BM3_SCREENS order.
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectSound<0> >;
        addHandler(28u, action);
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectSound<1> >;
        addHandler(29u, action);
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectBurbles<0> >;
        addHandler(30u, action);
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectBurbles<1> >;
        addHandler(31u, action);
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectBurbles<2> >;
        addHandler(32u, action);
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::selectBurbles<3> >;
        addHandler(33u, action);
        // BMW sends event 11 with a bool when this HMI state becomes visible or hidden.
        action.m_exec = &Action::execImpl<Bm3Maps, &Bm3Maps::pageVisibility>;
        addHandler(100u, 11u, action);
        status(kMapLine, "");
        status(kSoundLine, "");
        status(kBurbleLine, "");
        updateModel(kTilesModel, bmw::ImageID(kTiles));
        return true;
    }
    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
        if (key != kMapPoll) return PluginApp::invokeDataUpdate(key, data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation != generation_) return true;
        if (pipe_ >= 0) {
            ssize_t n = read(pipe_, result_ + used_, sizeof result_ - 1 - used_);
            if (n > 0) used_ += static_cast<size_t>(n);
            if (n == 0 || used_ == sizeof result_ - 1 ||
                (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                close(pipe_);
                pipe_ = -1;
                while (used_ && (result_[used_ - 1] == '\n' || result_[used_ - 1] == '\r')) --used_;
                result_[used_] = 0;
                status(used_ ? result_ : line_ == kMapLine ? "No answer. Check the active map before trying again."
                                                             : "No answer from the diagnostics worker.");
                // The tiles mark a map only while the last read confirmed it; any other answer leaves none marked.
                if (line_ == kMapLine) updateModel(kTilesModel, bmw::ImageID(kTiles + active_map(result_)));
            }
        }
        reap();
        if (pipe_ >= 0 || child_ > 0) postPoll();
        return true;
    }
};

extern "C" {
PLUGIN_EXPORT PluginAppInfo pluginAppInfo = {1, "Bm3Maps", "EntryNav-Apps", 1, create_app<Bm3Maps>, destroy_app};
}
