#include "gateway_host.h"
#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const unsigned kMapPoll = 0x4d48444du;
const unsigned kMapClear = 0x4d484443u;
#if defined(BM3_ONLY)
#define CONTROL_PLUGIN "Bootmod3Suite"
#define CONTROL_WORKER "bootmod3-qnx"
#elif defined(XDRIVE_ONLY)
#define CONTROL_PLUGIN "XDriveControl"
#define CONTROL_WORKER "xdrive-control-qnx"
#else
#define CONTROL_PLUGIN "MhdMaps"
#define CONTROL_WORKER "mhd-maps-qnx"
#endif
const char kMapWorker[] = "/var/opt/hmi/data/ID5/Plugins/" CONTROL_PLUGIN "/" CONTROL_WORKER;
}

class MhdMaps : public bmw::PluginApp {
    pid_t child_;
    int pipe_;
    unsigned generation_;
#ifdef PERF_LAB
    unsigned resultModel_;
#endif
    char result_[128];
    size_t used_;

    void status(const char* value) {
#ifdef PERF_LAB
        updateModel(resultModel_,bmw::BasicString<char>(value));
#else
        updateModel(50u,bmw::BasicString<char>(value));
#endif
    }
    void reap() {
        if (child_<=0) return;
        pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD)) child_=-1;
    }
    void stopWorker() {
        ++generation_;
        if (pipe_>=0) { close(pipe_); pipe_=-1; }
        reap();
        if (child_>0) kill(child_,SIGTERM);
    }
    void postStatusEvent(unsigned key, unsigned delay) {
        bmw::Parcel data;
        data.writeUInt32(key);
        data.writeUInt32(generation_);
        post(bmw::Message(0x40000003u,data),delay);
    }
    void postPoll() { postStatusEvent(kMapPoll,100u); }

    template <unsigned Value, unsigned Function> bool selectMap(const bmw::Parcel&) {
        reap();
        if (pipe_>=0 || child_>0) return true;
        ++generation_; // a prior result's dismissal cannot erase this request
        int channel[2];
        if (pipe(channel)<0) { status("Could not start map request"); return true; }
        const char argument[]={static_cast<char>('0'+Value),0};
#if defined(XDRIVE_ONLY)
        const char* mode=Function==1 ? "--status" : "--drive-mode";
#elif defined(BM3_ONLY)
        const char* mode=Function==1 ? "--map" : Function==2 ? "--flap" : Function==3 ? "--sound" : "--burbles";
#else
        const char* mode=Function==1 ? "--map" : Function==2 ? "--flap" : "--cooling";
#endif
#ifdef PERF_LAB
#ifdef XDRIVE_ONLY
        resultModel_=50u;
#else
        resultModel_=49u+Function;
#endif
#endif
        const char* args[]={kMapWorker,"--host",GATEWAY_HOST,mode,argument,0};
        if (fcntl(channel[0],F_SETFL,O_NONBLOCK)>=0 &&
            fcntl(channel[0],F_SETFD,FD_CLOEXEC)>=0 && fcntl(channel[1],F_SETFD,FD_CLOEXEC)>=0)
            child_=launch(args,channel[1]);
        close(channel[1]);
        if (child_<0) {
            close(channel[0]);
            status("Could not start map request");
            return true;
        }
        pipe_=channel[0];
        used_=0;
        char message[32];
        if (Function==1 && strcmp(mode,"--status")) snprintf(message,sizeof message,"Requesting map %u...",Value);
        else snprintf(message,sizeof message,"Sending request...");
        status(message);
        postPoll();
        return true;
    }

public:
    MhdMaps(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned maj,unsigned min,
            bmw::RHMIInterfaceVersion version,const bmw::Bundle& cfg,const bmw::Bundle& res)
        : PluginApp(d,name,maj,min,version,cfg,res),child_(-1),pipe_(-1),
          generation_(0),
#ifdef PERF_LAB
          resultModel_(50u),
#endif
          used_(0) {}
    virtual ~MhdMaps() { stopWorker(); }
    virtual bool onFinalize() { stopWorker(); return true; }
    virtual bool onInit() {
        stopWorker();
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        Action action;
        action.m_obj=this;
#ifdef XDRIVE_ONLY
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<0,1> >;
        addHandler(21u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<0,2> >;
        addHandler(22u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<1,2> >;
        addHandler(23u,action);
#else
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<1,1> >;
        addHandler(21u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<2,1> >;
        addHandler(22u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<3,1> >;
        addHandler(23u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<4,1> >;
        addHandler(24u,action);
#ifdef PERF_LAB
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<0,2> >;
        addHandler(25u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<1,2> >;
        addHandler(26u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<2,2> >;
        addHandler(27u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<0,3> >;
        addHandler(28u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<1,3> >;
        addHandler(29u,action);
#ifdef BM3_ONLY
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<0,4> >;
        addHandler(30u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<1,4> >;
        addHandler(31u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<2,4> >;
        addHandler(32u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<3,4> >;
        addHandler(33u,action);
#endif
#endif
#endif
        status("");
        if (child_>0) postPoll();
        return true;
    }
    virtual bool invokeDataUpdate(unsigned key,const bmw::Parcel& data) {
        if (key!=kMapPoll && key!=kMapClear) return PluginApp::invokeDataUpdate(key,data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation!=generation_) return true;
        if (key==kMapClear) { status(""); return true; }
        if (pipe_>=0) {
            ssize_t n=read(pipe_,result_+used_,sizeof result_-1-used_);
            if (n>0) used_+=static_cast<size_t>(n);
            else if (n==0 || (errno!=EINTR && errno!=EAGAIN && errno!=EWOULDBLOCK)) {
                close(pipe_);
                pipe_=-1;
                while (used_ && (result_[used_-1]=='\n' || result_[used_-1]=='\r')) --used_;
                result_[used_]=0;
                status(used_ ? result_ : "No result received from the diagnostic worker.");
                if (!strncmp(result_,"Confirmed: ",11)) postStatusEvent(kMapClear,3500u);
            }
        }
        reap();
        if (pipe_>=0 || child_>0) postPoll();
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo={1,CONTROL_PLUGIN,"owner-development",1,
                            create_app<MhdMaps>,destroy_app};
}
