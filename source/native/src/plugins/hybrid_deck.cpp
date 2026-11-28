#include "plugin_factory.h"
#ifdef ID6_COMPANION
#include "companion_surface.h"
#else
#define ID6_MENU_NAME "HybridDeck"
#endif
#include "diagnostic_gateway.h"
#include "diagnostic_worker.h"
#include "gauge_coexistence.h"
#include "gauge_core.h"
#include "hybrid_frame.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const char base[]="/var/opt/hmi/data/ID5/Plugins/" ID6_MENU_NAME "/";
const unsigned pollKey=0x4859444cu;
const unsigned pauseAfter=10u*60u*1000u;
const unsigned powerSlot=HYBRID_COUNT, statusSlot=powerSlot+1;
}

class HybridDeck : public bmw::PluginApp {
#ifdef ID6_COMPANION
    CompanionSurface companion_;
#endif
    int socket_;
    pid_t child_;
    unsigned generation_;
    bool visible_, stopping_;
    uint64_t opened_, retry_;
    hybrid_frame frame_;
    char displayed_[HYBRID_COUNT+2][80];

    void reap() {
        if (child_<=0) return;
        const pid_t ended=waitpid(child_,0,WNOHANG);
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

    void text(unsigned slot, const char *value) {
        if (strcmp(displayed_[slot],value)) {
            snprintf(displayed_[slot],sizeof displayed_[slot],"%s",value);
            updateModel(50u+slot,bmw::BasicString<char>(value));
#ifdef ID6_COMPANION
        companion_.update(50u+slot,value);
#endif
        }
    }

    void tick() {
        reap();
        if (!visible_ || socket_<0) return;
        const uint64_t now=bd_mono_ms();
        const bool paused=now-opened_>=pauseAfter;
        if (paused) stopWorker();
        if (!paused && child_<=0 && now>=retry_) {
            char gatewayPath[sizeof base+16], workerPath[sizeof base+27];
            snprintf(gatewayPath,sizeof gatewayPath,"%sgateway-ip.txt",base);
            snprintf(workerPath,sizeof workerPath,"%shybriddeck-collector-qnx",base);
            const DiagnosticGateway gateway(gatewayPath);
            const char *args[]={workerPath,"--host",gateway.host(),0};
            child_=launch(args,-1);
            retry_=now+2000;
        }
        for (unsigned i=0;i<8;++i) {
            hybrid_frame candidate;
            struct sockaddr_in peer;
            socklen_t size=sizeof peer;
            const ssize_t n=recvfrom(socket_,&candidate,sizeof candidate,0,
                reinterpret_cast<struct sockaddr*>(&peer),&size);
            if (n<0) break;
            if (!stopping_ && n==sizeof candidate && ntohs(peer.sin_port)==HYBRID_SOURCE_PORT)
                frame_=candidate;
        }

        static const char *const formats[]={"%.1f %%","%.1f V","%+.1f A","%.1f \260C","%.0f \260C","%.2f V","%.0f %%"};
        unsigned valid=0;
        char value[80];
        for (unsigned i=0;i<HYBRID_COUNT;++i) {
            if (!paused && frame_.sample[i]>=opened_ && frame_.sample[i]<=now &&
                now-frame_.sample[i]<(i==HYBRID_HEALTH ? HYBRID_HEALTH_STALE_MS : HYBRID_STALE_MS)) {
                valid|=1u<<i;
                snprintf(value,sizeof value,formats[i],frame_.value[i]);
                text(i,value);
            } else text(i,"--");
        }
        const uint64_t volts=frame_.sample[HYBRID_VOLTS], current=frame_.sample[HYBRID_AMPS];
        if ((valid & 6u)==6u && (volts>current ? volts-current : current-volts)<=1500) {
            // PHEV convention used by iDrive7 eDrive: SME current is negative on discharge.
            snprintf(value,sizeof value,"%+.1f kW",-frame_.value[HYBRID_VOLTS]*frame_.value[HYBRID_AMPS]/1000.0);
            text(powerSlot,value);
        } else text(powerSlot,"--");
        if (paused) text(statusSlot,"Paused. Choose Refresh in Options.");
        else if (valid & (15u | (1u<<HYBRID_HEALTH))) text(statusSlot,"Pack power: + discharge / - charge");
        else if (now-opened_<5000) text(statusSlot,"Reading the hybrid battery...");
        else if (valid) text(statusSlot,"Hybrid battery readings unavailable");
        else text(statusSlot,"No readings from the diagnostic gateway");
    }

    void postPoll() {
        bmw::Parcel data;
        data.writeUInt32(pollKey);
        data.writeUInt32(generation_);
        post(bmw::Message(0x40000003u,data),visible_ ? 200u : 400u);
    }

    bool refresh(const bmw::Parcel&) {
        if (visible_) {
            opened_=bd_mono_ms();
            retry_=0;
            memset(&frame_,0,sizeof frame_);
            tick();
        }
        return true;
    }

    bool visibility(const bmw::Parcel& data) {
        bool visible;
        if (data.readBool(visible) && visible!=visible_) {
            visible_=visible;
            if (visible_) refresh(data);
            else stopWorker();
        }
        return true;
    }

public:
    HybridDeck(bmw::IDispatcher& d,bmw::BasicString<char> name,unsigned maj,unsigned min,
            bmw::RHMIInterfaceVersion version,const bmw::Bundle& cfg,const bmw::Bundle& res)
        : PluginApp(d,name,maj,min,version,cfg,res),socket_(-1),child_(-1),generation_(0),
          visible_(false),stopping_(false),opened_(0),retry_(0) {}
    virtual ~HybridDeck() { shutdown(); }
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
        memset(displayed_,0,sizeof displayed_);
        memset(&frame_,0,sizeof frame_);
        socket_=gauge_bind(HYBRID_PORT);
        typedef bmw::Delegate<bool,const bmw::Parcel&> Handler;
        Handler handler;
        handler.m_obj=this;
        handler.m_exec=&Handler::execImpl<HybridDeck,&HybridDeck::visibility>;
        addHandler(100u,11u,handler);
        handler.m_exec=&Handler::execImpl<HybridDeck,&HybridDeck::refresh>;
        addHandler(21u,handler);
        for (unsigned i=0;i<=powerSlot;++i) text(i,"--");
        text(statusSlot,socket_>=0 ? "Waiting for battery readings" : "Telemetry unavailable");
        if (socket_>=0) postPoll();
        return true;
    }
    virtual bool invokeDataUpdate(unsigned key,const bmw::Parcel& data) {
#ifdef ID6_COMPANION
        if (companion_.poll(key)) return true;
#endif
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
PluginAppInfo pluginAppInfo={1,ID6_MENU_NAME,"owner-development",1,
                            create_app<HybridDeck>,destroy_app};
}
