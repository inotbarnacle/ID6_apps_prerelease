#include "plugin_factory.h"
#include "diagnostic_gateway.h"
#include "diagnostic_worker.h"
#include "gauge_coexistence.h"
#include "gauge_core.h"
#include "warm_progress.h"
#include <math.h>
#include <stdio.h>
#include <sys/wait.h>

namespace {
const char worker[]="/var/opt/hmi/data/ID5/Plugins/WarmDeck/warmdeck-collector-qnx";
const char gatewayFile[]="/var/opt/hmi/data/ID5/Plugins/WarmDeck/gateway-ip.txt";
const unsigned pollKey=0x5744504cu;
}

class WarmDeck : public bmw::PluginApp {
    int socket_;
    pid_t child_;
    unsigned generation_;
    bool visible_, stopping_;
    uint64_t opened_, retry_;
    warm_frame frame_;
    WarmProgress progress_;
    char displayed_[5][80];
    unsigned bars_[WARM_COUNT];

    void reap() {
        if (child_<=0) return;
        pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD)) {
            child_=-1;
            stopping_=false;
        }
    }

    void stopWorker() {
        reap();
        if (child_>0 && !stopping_) {
            kill(child_,SIGTERM);
            stopping_=true;
        }
    }

    void shutdown() {
        visible_=false;
        ++generation_;
        stopWorker();
        if (socket_>=0) close(socket_);
        socket_=-1;
    }

    void postPoll() {
        bmw::Parcel parcel;
        parcel.writeUInt32(pollKey);
        parcel.writeUInt32(generation_);
        post(bmw::Message(0x40000003u,parcel),visible_ ? 100u : 250u);
    }

    bool visibility(const bmw::Parcel& e) {
        bool newVisibility;
        if (e.readBool(newVisibility) && newVisibility!=visible_) {
            visible_=newVisibility;
            memset(&frame_,0,sizeof frame_);
            progress_.reset();
            opened_=bd_mono_ms();
            retry_=0;
            if (!visible_) stopWorker();
            else tick();
        }
        return true;
    }

    void text(unsigned slot, const char* value) {
        if (strcmp(displayed_[slot],value)) {
            snprintf(displayed_[slot],sizeof displayed_[slot],"%s",value);
            updateModel(50u+slot,bmw::BasicString<char>(value));
        }
    }

    void tick() {
        reap();
        if (!visible_ || socket_<0) return;
        const uint64_t now=bd_mono_ms();
        if (child_<=0 && now>=retry_) {
            const DiagnosticGateway gateway(gatewayFile);
            const char* args[]={worker,"--host",gateway.host(),0};
            child_=launch(args,-1);
            retry_=now+2000;
        }
        for (unsigned i=0;i<8;++i) {
            warm_frame candidate;
            struct sockaddr_in peer;
            socklen_t size=sizeof peer;
            ssize_t length=recvfrom(socket_,&candidate,sizeof candidate,0,
                reinterpret_cast<struct sockaddr*>(&peer),&size);
            if (length<0) break;
            if (length==sizeof candidate && ntohs(peer.sin_port)==WARM_ACTIVE_PORT && !stopping_)
                frame_=candidate;
        }
        unsigned valid=0;
        for (unsigned i=0;i<WARM_COUNT;++i) {
            char value[80]="--";
            unsigned step=0;
            if (frame_.sample[i]>=opened_ && frame_.sample[i]<=now &&
                now-frame_.sample[i]<WARM_STALE_MS &&
                frame_.celsius[i]>=-48 && frame_.celsius[i]<=180) {
                valid|=1u<<i;
                snprintf(value,sizeof value,"%.1f \260C",frame_.celsius[i]);
                const double fraction=frame_.celsius[i]/kWarmTarget[i];
                step=fraction>=1 ? 20u : fraction>0 ? static_cast<unsigned>(fraction*20+0.5) : 0u;
            }
            text(i,value);
            if (step!=bars_[i]) {
                updateModel(60u+i,bmw::ImageID(58100u+step));
                bars_[i]=step;
            }
        }
        const int eta=progress_.update(frame_,valid);
        char status[80];
        if (!valid) strcpy(status,"Waiting for temperature readings");
        else if (valid!=(1u<<WARM_COUNT)-1) strcpy(status,"Waiting for all three temperatures");
        else if (!eta) strcpy(status,"Temperature targets reached");
        else if (eta<0) strcpy(status,"Measuring warm-up trend");
        else snprintf(status,sizeof status,"Estimated warm-up  %d:%02d",eta/60,eta%60);
        text(3,status);
        if ((valid&1u) && eta!=0)
            snprintf(status,sizeof status,"Warm-up guide  %u RPM",frame_.celsius[WARM_OIL]<60 ? 2500u : 3500u);
        else status[0]=0;
        text(4,status);
    }

public:
    WarmDeck(bmw::IDispatcher& dispatcher, bmw::BasicString<char> name,
             unsigned major, unsigned minor, bmw::RHMIInterfaceVersion version,
             const bmw::Bundle& config, const bmw::Bundle& resources)
        : PluginApp(dispatcher,name,major,minor,version,config,resources),socket_(-1),
          child_(-1),generation_(0),visible_(false),stopping_(false),opened_(0),retry_(0) {}

    virtual ~WarmDeck() { shutdown(); }
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool onInit() {
        shutdown();
        memset(displayed_,0,sizeof displayed_);
        memset(bars_,0,sizeof bars_);
        memset(&frame_,0,sizeof frame_);
        socket_=gauge_bind(WARM_PORT);
        typedef bmw::Delegate<bool,const bmw::Parcel&> Handler;
        Handler show;
        show.m_obj=this;
        show.m_exec=&Handler::execImpl<WarmDeck,&WarmDeck::visibility>;
        addHandler(100u,11u,show);
        for (unsigned i=0;i<WARM_COUNT;++i) text(i,"--");
        text(3,socket_>=0 ? "Waiting for temperature readings" : "Telemetry unavailable");
        if (socket_>=0) postPoll();
        return true;
    }

    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
        if (key!=pollKey) return PluginApp::invokeDataUpdate(key,data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation==generation_ && socket_>=0) {
            tick();
            postPoll();
        }
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo={1,"WarmDeck","owner-development",1,
                             create_app<WarmDeck>,destroy_app};
}
