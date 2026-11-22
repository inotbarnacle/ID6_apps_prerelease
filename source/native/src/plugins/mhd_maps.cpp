#include "plugin_factory.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const unsigned kMapPoll = 0x4d48444du;
const unsigned kMapClear = 0x4d484443u;
const char kMapWorker[] = "/var/opt/hmi/data/ID5/Plugins/MhdMaps/mhd-maps-qnx";
const char kFlapWorker[] = "/var/opt/hmi/data/ID5/Plugins/MhdMaps/exhaust-flap-worker-qnx";
}

class MhdMaps : public bmw::PluginApp {
    pid_t child_;
    int pipe_;
    unsigned generation_;
    unsigned resultModel_;
    char result_[128];
    size_t used_;
    unsigned acceptedMap_; // 0 until the DME echoes a map request
    int acceptedCooling_; // -1 until the DME echoes a cooling request

    void status(const char* value) {
        updateModel(resultModel_,bmw::BasicString<char>(value));
    }
    void showAcceptedSettings() {
        const char* cooling=acceptedCooling_<0 ? "Unknown" : acceptedCooling_ ? "On" : "Off";
        char map[12]="Unknown";
        if (acceptedMap_) snprintf(map,sizeof map,"%u",acceptedMap_);
        char text[80];
        snprintf(text,sizeof text,"Last accepted   |   Map %s   |   Max Cooling %s",map,cooling);
        updateModel(53u,bmw::BasicString<char>(text));
    }
    void rememberAcceptedSettings() {
        const char prefix[]="Accepted by DME: Map ";
        if (resultModel_==50u && !strncmp(result_,prefix,sizeof prefix-1) &&
            strlen(result_)==sizeof prefix && result_[sizeof prefix-1]>='1' &&
            result_[sizeof prefix-1]<='4') {
            acceptedMap_=static_cast<unsigned>(result_[sizeof prefix-1]-'0');
        } else if (resultModel_==52u) {
            if (!strcmp(result_,"Accepted by DME: Max Cooling on")) acceptedCooling_=1;
            else if (!strcmp(result_,"Accepted by DME: Max Cooling off")) acceptedCooling_=0;
            else return;
        } else return;
        showAcceptedSettings();
    }
    void coolingTiles(const char* reply) {
        updateModel(63u,bmw::ImageID(!strcmp(reply,"Accepted by DME: Max Cooling off") ? 56011u : 56010u));
        updateModel(64u,bmw::ImageID(!strcmp(reply,"Accepted by DME: Max Cooling on") ? 56013u : 56012u));
    }
    void flapTiles(const char* selected) {
        updateModel(66u,bmw::ImageID(!strcmp(selected,"Confirmed: Exhaust open") ? 56201u : 56200u));
        updateModel(67u,bmw::ImageID(!strcmp(selected,"Confirmed: Exhaust auto") ? 56203u : 56202u));
        updateModel(68u,bmw::ImageID(!strcmp(selected,"Confirmed: Exhaust closed") ? 56205u : 56204u));
    }
    void reap() {
        if (child_<=0) return;
        pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD && kill(child_,0)!=0 && errno==ESRCH)) child_=-1;
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
        resultModel_=Function==5 ? 50u : 49u+Function;
        if (Function==3) coolingTiles("");
        int channel[2];
        if (pipe(channel)<0) { status("Could not start map request"); return true; }
        const char argument[]={static_cast<char>('0'+Value),0};
        const char* mode=Function==1 ? "--map" : Function==2 ? "--flap" : "--cooling";
        const DiagnosticGateway gateway("/var/opt/hmi/data/ID5/Plugins/MhdMaps/gateway-ip.txt");
        const char* worker=kMapWorker;
        if (Function==2) worker=kFlapWorker;
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
        char message[32];
        if (Function==1 && strcmp(mode,"--status")) snprintf(message,sizeof message,"Requesting map %u...",Value);
        else if (Function==5) snprintf(message,sizeof message,"Reading current map...");
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
          resultModel_(50u),
          used_(0)
          ,acceptedMap_(0),acceptedCooling_(-1)
           {}
    virtual ~MhdMaps() { stopWorker(); }
    virtual bool onFinalize() { stopWorker(); return true; }
    virtual bool onInit() {
        stopWorker();
        typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
        Action action;
        action.m_obj=this;
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<1,1> >;
        addHandler(21u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<2,1> >;
        addHandler(22u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<3,1> >;
        addHandler(23u,action);
        action.m_exec=&Action::execImpl<MhdMaps,&MhdMaps::selectMap<4,1> >;
        addHandler(24u,action);
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
        status("");
        coolingTiles("");
        flapTiles("");
        showAcceptedSettings();
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
                rememberAcceptedSettings();
                if (resultModel_==52u) coolingTiles(result_);
                if (resultModel_==51u && !strncmp(result_,"Confirmed: Exhaust ",19)) flapTiles(result_);
                status(used_ ? result_ : "No result received from the diagnostic worker.");
                if (!strncmp(result_,"Confirmed: ",11) || !strncmp(result_,"Accepted by DME: Map ",21))
                    postStatusEvent(kMapClear,3500u);
            }
        }
        reap();
        if (pipe_>=0 || child_>0) postPoll();
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo={1,"MhdMaps","owner-development",1,
                            create_app<MhdMaps>,destroy_app};
}
