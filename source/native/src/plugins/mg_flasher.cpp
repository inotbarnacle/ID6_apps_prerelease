#include "plugin_factory.h"
#ifdef ID6_COMPANION
#include "companion_surface.h"
#else
#define ID6_MENU_NAME "MgFlasher"
#endif
#include "diagnostic_gateway.h"
#include "diagnostic_worker.h"
#include "gauge_preferences.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const char worker[]="/var/opt/hmi/data/ID5/Plugins/" ID6_MENU_NAME "/mgflasher-qnx";
const char gatewayFile[]="/var/opt/hmi/data/ID5/Plugins/" ID6_MENU_NAME "/gateway-ip.txt";
const char profile[]="/var/opt/hmi/data/rw/mgflasher-live-profile.txt";
const char *const modes[]={"--status","--map","--burbles","--cooling","--antilag","--exhaust",
                          "--valet","--ethanol-enable","--ethanol-value","--radiator"};
const unsigned pollKey=0x4d47464cu;
}

class MgFlasher : public bmw::PluginApp {
#ifdef ID6_COMPANION
    CompanionSurface companion_;
#endif
    pid_t child_;
    int pipe_;
    unsigned generation_, model_;
    char result_[512];
    size_t used_;

    void status(const char *value) {
        updateModel(model_,bmw::BasicString<char>(value));
#ifdef ID6_COMPANION
        companion_.update(model_,value);
#endif
    }

    void reap() {
        if (child_<=0) return;
        const pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD)) child_=-1;
    }

    void shutdown() {
        ++generation_;
        if (pipe_>=0) { close(pipe_); pipe_=-1; }
        reap();
        if (child_>0) kill(child_,SIGTERM);
    }

    void postPoll() {
        bmw::Parcel data;
        data.writeUInt32(pollKey);
        data.writeUInt32(generation_);
        post(bmw::Message(0x40000003u,data),120u);
    }

    template<unsigned Group, unsigned Choice> bool request(const bmw::Parcel&) {
        reap();
        if (child_>0 || pipe_>=0) return true;
        model_=50u+Group;
        int channel[2];
        if (pipe(channel)<0) { status("Could not start diagnostics"); return true; }
        ++generation_;
        char value[4];
        snprintf(value,sizeof value,"%u",Choice);
        const DiagnosticGateway gateway(gatewayFile);
        const char *args[]={worker,"--host",gateway.host(),"--profile",profile,modes[Group],value,0};
        if (fcntl(channel[0],F_SETFL,O_NONBLOCK)>=0 &&
            fcntl(channel[0],F_SETFD,FD_CLOEXEC)>=0 && fcntl(channel[1],F_SETFD,FD_CLOEXEC)>=0)
            child_=launch(args,channel[1]);
        close(channel[1]);
        if (child_<0) {
            close(channel[0]);
            status("Could not start diagnostics");
            return true;
        }
        pipe_=channel[0];
        used_=0;
        status(Group==0 ? "Reading Custom Code settings..." : "Sending live command...");
        postPoll();
        return true;
    }

    template<unsigned Generation> bool setProfile(const bmw::Parcel&) {
        reap();
        if (child_>0 || pipe_>=0) return true;
        const char *const choices[]={"MGF73 GEN1\n","MGF73 GEN2\n","disabled\n"};
        const char *option=choices[Generation];
        const bool saved=savePersistentBytes(profile,reinterpret_cast<const unsigned char*>(option),
                                             strlen(option));
        const char *const labels[]={"Gen 1 selected.","Gen 2 selected.","Live controls disabled."};
        updateModel(69u,bmw::BasicString<char>(saved ? labels[Generation] : "Could not save the DME profile."));
#ifdef ID6_COMPANION
        companion_.update(69u,saved ? labels[Generation] : "Could not save the DME profile.");
#endif
        return true;
    }

public:
    MgFlasher(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned maj,unsigned min,
              bmw::RHMIInterfaceVersion version,const bmw::Bundle& cfg,const bmw::Bundle& res)
        : PluginApp(d,name,maj,min,version,cfg,res),child_(-1),pipe_(-1),
          generation_(0),model_(50u),used_(0) {}
    virtual ~MgFlasher() { shutdown(); }
    virtual bool onFinalize() {
#ifdef ID6_COMPANION
        companion_.close();
#endif
        shutdown();
        return true;
    }
    virtual bool onInit() {
#ifdef ID6_COMPANION
        companion_.bind(*this);
#endif
        shutdown();
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        Action action;
        action.m_obj=this;
        typedef bool (*Handler)(void*,const bmw::Parcel&);
        static const struct { unsigned id; Handler function; } routes[]={
#define MG_ACTION(id,group,choice) {id,&Action::execImpl<MgFlasher,&MgFlasher::request<group,choice> >},
#include "mg_menu_routes.inc"
#undef MG_ACTION
            {91,&Action::execImpl<MgFlasher,&MgFlasher::setProfile<0> >},
            {92,&Action::execImpl<MgFlasher,&MgFlasher::setProfile<1> >},
            {93,&Action::execImpl<MgFlasher,&MgFlasher::setProfile<2> >},

        };
        for (unsigned i=0;i<sizeof routes/sizeof routes[0];++i) {
            action.m_exec=routes[i].function;
            addHandler(routes[i].id,action);
        }
        for (model_=50u;model_<60u;++model_) status("");
        model_=50u;
        status("Select the matching DME profile, then refresh current settings.");
        updateModel(69u,bmw::BasicString<char>("Select only if the DME runs MG Custom Code 7.3–7.3.4."));
        if (child_>0) postPoll();
        return true;
    }

    virtual bool invokeDataUpdate(unsigned key,const bmw::Parcel& data) {
#ifdef ID6_COMPANION
        if (companion_.poll(key)) return true;
#endif
        if (key!=pollKey) return PluginApp::invokeDataUpdate(key,data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation!=generation_) return true;
        if (pipe_>=0) {
            const ssize_t n=read(pipe_,result_+used_,sizeof result_-1-used_);
            if (n>0) used_+=static_cast<size_t>(n);
            else if (n==0 || (errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK)) {
                close(pipe_);
                pipe_=-1;
                while (used_ && (result_[used_-1]=='\n' || result_[used_-1]=='\r')) --used_;
                result_[used_]=0;
                status(used_ ? result_ : "No result received from diagnostics");
            }
        }
        reap();
        if (pipe_>=0 || child_>0) postPoll();
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo={1,ID6_MENU_NAME,"owner-development",1,
                            create_app<MgFlasher>,destroy_app};
}
