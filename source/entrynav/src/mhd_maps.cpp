// MHD+ Suite (the MhdMaps plugin): MHD+ live controls from iDrive. The mhd-maps worker does the diagnostics
// (collector.c, MHD_MAPS_ONLY); this plugin starts it and shows the line it prints. A tile asks the DME for that map.
// OPTIONS has Max Cooling on its own screen with its own result line. MHD has no known read-back of the active map, so
// no tile is marked and the line says what the DME accepted. One worker at a time: a press while one runs only says so.
// Independent of and unaffiliated with MHD.
#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const unsigned kDataUpdateMessage = 0x40000003u;
const unsigned kMapPoll = 0x4d48444du;  // MHDM, private to this app
const char kMapWorker[] = ENA_MHDMAPS_WORKER;

// The result lines: the main page's, then Max Cooling's (tools/make_drive_apps.py, MHD_SCREENS).
const unsigned kMapLine = 50u, kCoolingLine = 52u;
}

class MhdMaps : public bmw::PluginApp {
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
        snprintf(message, sizeof message, "Asking for map %u...", Slot);
        run(kMapLine, "--map", slot, message);
        return true;
    }
    template <unsigned On> bool selectCooling(const bmw::Parcel&) {
        static const char on[] = {static_cast<char>('0' + On), 0};
        run(kCoolingLine, "--cooling", on, "Sending the Max Cooling request...");
        return true;
    }
    // Nothing tells which map is active, so an earlier answer is cleared rather than left to look current.
    bool pageVisibility(const bmw::Parcel& event) {
        bool visible;
        if (!event.readBool(visible) || !visible) return true;
        reap();
        if (pipe_ < 0 && child_ <= 0) status(kMapLine, "");
        return true;
    }

public:
    MhdMaps(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned maj, unsigned min,
            bmw::RHMIInterfaceVersion version, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : PluginApp(d, name, maj, min, version, cfg, res), child_(-1), pipe_(-1), generation_(0), line_(kMapLine),
          used_(0) {}
    virtual ~MhdMaps() { stopWorker(); }
    virtual bool onFinalize() {
        stopWorker();
        return true;
    }
    virtual bool onInit() {
        stopWorker();
        typedef bmw::Delegate<bool, const bmw::Parcel&> Action;
        Action action;
        action.m_obj = this;
        action.m_exec = &Action::execImpl<MhdMaps, &MhdMaps::selectMap<1> >;
        addHandler(21u, action);
        action.m_exec = &Action::execImpl<MhdMaps, &MhdMaps::selectMap<2> >;
        addHandler(22u, action);
        action.m_exec = &Action::execImpl<MhdMaps, &MhdMaps::selectMap<3> >;
        addHandler(23u, action);
        action.m_exec = &Action::execImpl<MhdMaps, &MhdMaps::selectMap<4> >;
        addHandler(24u, action);
        // The Max Cooling screen's buttons, in tools/make_drive_apps.py's MHD_SCREENS order.
        action.m_exec = &Action::execImpl<MhdMaps, &MhdMaps::selectCooling<0> >;
        addHandler(28u, action);
        action.m_exec = &Action::execImpl<MhdMaps, &MhdMaps::selectCooling<1> >;
        addHandler(29u, action);
        // BMW sends event 11 with a bool when this HMI state becomes visible or hidden.
        action.m_exec = &Action::execImpl<MhdMaps, &MhdMaps::pageVisibility>;
        addHandler(100u, 11u, action);
        status(kMapLine, "");
        status(kCoolingLine, "");
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
            }
        }
        reap();
        if (pipe_ >= 0 || child_ > 0) postPoll();
        return true;
    }
};

extern "C" {
PLUGIN_EXPORT PluginAppInfo pluginAppInfo = {1, "MhdMaps", "EntryNav-Apps", 1, create_app<MhdMaps>, destroy_app};
}
