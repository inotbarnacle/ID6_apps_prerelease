#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const unsigned kMapPoll = 0x4d48444du;
const unsigned kMapClear = 0x4d484443u;
const char kMapWorker[] = "/var/opt/hmi/data/ID5/Plugins/Bootmod3Suite/bootmod3-qnx";
}

class Bootmod3SuiteApp : public bmw::PluginApp {
    pid_t child_;
    int pipe_;
    unsigned generation_;
    unsigned resultModel_;
    unsigned pendingMap_;
    bool readingMap_;
    char result_[128];
    size_t used_;

    void status(const char* value) {
        updateModel(resultModel_,bmw::BasicString<char>(value));
    }
    void reap() {
        if (child_<=0) return;
        pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD)) child_=-1;
    }
    void stopWorker() {
        ++generation_;
        pendingMap_=0;
        readingMap_=false;
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

    bool startRequest(unsigned value, unsigned function) {
        reap();
        if (pipe_>=0 || child_>0) {
            if (function==1 && readingMap_) pendingMap_=value;
            return true;
        }
        ++generation_; // a prior result's dismissal cannot erase this request
        resultModel_=function==5 ? 50u : 49u+function;
        int channel[2];
        if (pipe(channel)<0) { status("Could not start map request"); return true; }
        const char argument[]={static_cast<char>('0'+value),0};
        const char* mode=function==1 ? "--map" : function==2 ? "--flap" : function==3 ? "--sound" :
                         function==4 ? "--burbles" : "--current-map";
        const DiagnosticGateway gateway("/var/opt/hmi/data/ID5/Plugins/Bootmod3Suite/gateway-ip.txt");
        const char* worker=kMapWorker;
        const char* args[]={worker,"--host",gateway.host(),mode,argument,0};
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
        readingMap_=function==5;
        char message[32];
        if (function==1) snprintf(message,sizeof message,"Requesting map %u...",value);
        else if (function==5) snprintf(message,sizeof message,"Reading current map...");
        else snprintf(message,sizeof message,"Sending request...");
        status(message);
        postPoll();
        return true;
    }

    template <unsigned Value, unsigned Function> bool selectMap(const bmw::Parcel&) {
        return startRequest(Value,Function);
    }

    bool readMapOnEntry(const bmw::Parcel& event) {
        bool visible;
        if (event.readBool(visible)) {
            if (visible) return startRequest(0,5);
            pendingMap_=0;
        }
        return true;
    }

public:
    Bootmod3SuiteApp(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned maj,unsigned min,
            bmw::RHMIInterfaceVersion version,const bmw::Bundle& cfg,const bmw::Bundle& res)
        : PluginApp(d,name,maj,min,version,cfg,res),child_(-1),pipe_(-1),
          generation_(0),
          resultModel_(50u),
          pendingMap_(0),
          readingMap_(false),
          used_(0)
           {}
    virtual ~Bootmod3SuiteApp() { stopWorker(); }
    virtual bool onFinalize() { stopWorker(); return true; }
    virtual bool onInit() {
        stopWorker();
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        Action action;
        action.m_obj=this;
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<1,1> >;
        addHandler(21u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<2,1> >;
        addHandler(22u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<3,1> >;
        addHandler(23u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<4,1> >;
        addHandler(24u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<0,2> >;
        addHandler(25u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<1,2> >;
        addHandler(26u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<2,2> >;
        addHandler(27u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<0,3> >;
        addHandler(28u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<1,3> >;
        addHandler(29u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<0,4> >;
        addHandler(30u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<1,4> >;
        addHandler(31u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<2,4> >;
        addHandler(32u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::selectMap<3,4> >;
        addHandler(33u,action);
        action.m_exec=&Action::execImpl<Bootmod3SuiteApp,&Bootmod3SuiteApp::readMapOnEntry>;
        addHandler(100u,11u,action);
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
                if (!strncmp(result_,"Confirmed: ",11) || !strncmp(result_,"Accepted by DME: Map ",21))
                    postStatusEvent(kMapClear,3500u);
            }
        }
        reap();
        if (pipe_>=0 || child_>0) postPoll();
        else {
            readingMap_=false;
            if (pendingMap_) {
                const unsigned value=pendingMap_;
                pendingMap_=0;
                return startRequest(value,1);
            }
        }
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo={1,"Bootmod3Suite","owner-development",1,
                            create_app<Bootmod3SuiteApp>,destroy_app};
}
