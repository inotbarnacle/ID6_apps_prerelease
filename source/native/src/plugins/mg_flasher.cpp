#include "plugin_factory.h"
#include "diagnostic_gateway.h"
#include "diagnostic_worker.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const char worker[]="/var/opt/hmi/data/ID5/Plugins/MgFlasher/mgflasher-qnx";
const char gatewayFile[]="/var/opt/hmi/data/ID5/Plugins/MgFlasher/gateway-ip.txt";
const char profile[]="/var/opt/hmi/data/rw/mgflasher-live-profile.txt";
const char *const modes[]={"--status","--map","--burbles","--cooling"};
const unsigned pollKey=0x4d47464cu;
}

class MgFlasher : public bmw::PluginApp {
    pid_t child_;
    int pipe_;
    unsigned generation_, model_;
    char result_[192];
    size_t used_;

    void status(const char *value) {
        updateModel(model_,bmw::BasicString<char>(value));
    }

    void reap() {
        if (child_<=0) return;
        const pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD && kill(child_,0)!=0 && errno==ESRCH)) child_=-1;
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
        const char value[]={static_cast<char>('0'+Choice),0};
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

public:
    MgFlasher(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned maj,unsigned min,
              bmw::RHMIInterfaceVersion version,const bmw::Bundle& cfg,const bmw::Bundle& res)
        : PluginApp(d,name,maj,min,version,cfg,res),child_(-1),pipe_(-1),
          generation_(0),model_(50u),used_(0) {}
    virtual ~MgFlasher() { shutdown(); }
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool onInit() {
        shutdown();
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        Action action;
        action.m_obj=this;
        typedef bool (*Handler)(void*,const bmw::Parcel&);
        static const struct { unsigned id; Handler function; } routes[]={
#define SET(id,group,choice) {id,&Action::execImpl<MgFlasher,&MgFlasher::request<group,choice> >}
            SET(20,0,0), SET(21,1,0), SET(22,1,1), SET(23,1,2), SET(24,1,3), SET(25,1,4),
            SET(30,2,0), SET(31,2,1), SET(32,2,2), SET(33,2,3), SET(34,2,4), SET(35,2,5),
            SET(40,3,0), SET(41,3,1),
#undef SET
        };
        for (unsigned i=0;i<sizeof routes/sizeof routes[0];++i) {
            action.m_exec=routes[i].function;
            addHandler(routes[i].id,action);
        }
        for (model_=50u;model_<54u;++model_) status("");
        model_=50u;
        status("Select a live control. Refresh is available with readback locators.");
        if (child_>0) postPoll();
        return true;
    }

    virtual bool invokeDataUpdate(unsigned key,const bmw::Parcel& data) {
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
PluginAppInfo pluginAppInfo={1,"MgFlasher","owner-development",1,
                            create_app<MgFlasher>,destroy_app};
}
