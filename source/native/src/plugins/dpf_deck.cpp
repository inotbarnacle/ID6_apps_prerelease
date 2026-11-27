#include "plugin_factory.h"
#include "diagnostic_gateway.h"
#include "diagnostic_worker.h"
#include "gauge_coexistence.h"
#include "gauge_core.h"
#include "gauge_preferences.h"
#include "dpf_core.h"
#include <stdio.h>
#include <sys/wait.h>

namespace {
const char dpfWorker[]="/var/opt/hmi/data/ID5/Plugins/DPFDeck/dpfdeck-collector-qnx";
const char dpfGateway[]="/var/opt/hmi/data/ID5/Plugins/DPFDeck/gateway-ip.txt";
const char dpfPreferences[]="/var/opt/hmi/data/ID5/DPFDeck.prefs";
const unsigned dpfPollKey=0x44504650u;
unsigned dpfLaunchSerial;
}

class DPFDeck : public bmw::PluginApp {
    const char *preferences_;
    int socket_;
    pid_t child_;
    unsigned generation_,visible_,bank_,labels_known_;
    bool metric_,regen_,stopping_,save_error_;
    uint64_t token_,opened_,retry_,bind_retry_;
    dpf_frame frame_;
    char labels_[12][96];

    void text(unsigned slot,const char *value) {
        if ((labels_known_ & (1u<<slot)) && !strcmp(labels_[slot],value)) return;
        labels_known_|=1u<<slot;
        snprintf(labels_[slot],sizeof labels_[slot],"%s",value);
        updateModel(50u+slot,bmw::BasicString<char>(value));
    }

    void clearFrame() { memset(&frame_,0,sizeof frame_); }

    void reap() {
        if (child_<=0) return;
        const pid_t ended=waitpid(child_,0,WNOHANG);
        /* Reuse #143's ownership rule: ECHILD alone does not prove the
         * diagnostic process is gone. Inconclusive/EPERM probes retain it. */
        if (ended==child_ || (ended<0 && errno==ECHILD && kill(child_,0)!=0 && errno==ESRCH)) {
            child_=-1;stopping_=false;token_=0;clearFrame();
        }
    }

    void stopWorker() {
        reap();
        if (child_>0 && !stopping_) { kill(child_,SIGTERM);stopping_=true; }
        clearFrame();
    }

    void shutdown() {
        visible_=0;++generation_;
        stopWorker();
        if (socket_>=0) close(socket_);
        socket_=-1;
    }

    void postPoll() {
        bmw::Parcel parcel;
        parcel.writeUInt32(dpfPollKey);parcel.writeUInt32(generation_);
        post(bmw::Message(0x40000003u,parcel),visible_ ? 100u : 250u);
    }

    template <unsigned State> bool visibility(const bmw::Parcel &event) {
        bool shown;
        if (!event.readBool(shown)) return false;
        const unsigned before=visible_,bit=1u<<State;
        if (shown) visible_|=bit;
        else visible_&=~bit;
        if (!before && visible_) {
            retry_=0;
            if (child_<=0 || stopping_) { opened_=bd_mono_ms();clearFrame(); }
        }
        /* Reconcile on the next bounded poll. Native OPTIONS transitions may
         * hide the dashboard before showing the options state. */
        return true;
    }

    void loadPreferences() {
        bank_=0;metric_=true;regen_=false;save_error_=false;
        FILE *file=fopen(preferences_,"rb");
        if (!file) return;
        unsigned char bytes[5];
        const size_t size=fread(bytes,1,sizeof bytes,file);
        const bool valid=size>=4 && !ferror(file) &&
            (feof(file) || (fgetc(file)==EOF && !ferror(file))) &&
            bytes[0]=='D' && bytes[2]<2 && bytes[3]<2 &&
            ((size==4 && bytes[1]==1) || (size==5 && bytes[1]==2 && bytes[4]<2));
        fclose(file);
        if (valid) { bank_=bytes[2];metric_=bytes[3]!=0;regen_=size==5 && bytes[4]!=0; }
    }

    bool savePreferences(unsigned bank,bool metric,bool regen) {
        const unsigned char bytes[]={'D',2,static_cast<unsigned char>(bank),static_cast<unsigned char>(metric),static_cast<unsigned char>(regen)};
        if (!savePersistentBytes(preferences_,bytes,sizeof bytes)) { save_error_=true;return false; }
        bank_=bank;metric_=metric;regen_=regen;save_error_=false;
        return true;
    }

    template <unsigned Bank> bool bank(const bmw::Parcel &) {
        savePreferences(Bank,metric_,regen_);render(bd_mono_ms());return true;
    }
    template <bool Metric> bool units(const bmw::Parcel &) {
        savePreferences(bank_,Metric,regen_);render(bd_mono_ms());return true;
    }
    template <bool Show> bool regenStatus(const bmw::Parcel &) {
        const bool changed=Show!=regen_;
        if (savePreferences(bank_,metric_,Show) && changed) {
            stopWorker();retry_=0;opened_=bd_mono_ms();
        }
        render(bd_mono_ms());return true;
    }

    bool accept(const char *packet,size_t size,const sockaddr_in &peer,uint64_t now) {
        dpf_frame next;
        if (!visible_ || stopping_ || child_<=0 || !token_ || peer.sin_family!=AF_INET ||
            peer.sin_addr.s_addr!=htonl(INADDR_LOOPBACK) || ntohs(peer.sin_port)!=DPF_ACTIVE_PORT ||
            !dpf_parse(packet,size,&next) || next.worker_pid!=static_cast<uint32_t>(child_) || next.token!=token_ ||
            next.sent_ms<opened_ || next.sent_ms>now || now-next.sent_ms>=DPF_STALE_MS) return false;
        if (frame_.sent_ms) {
            const uint32_t advance=next.seq-frame_.seq;
            if (!advance || advance>UINT32_C(0x7fffffff) || next.sent_ms<frame_.sent_ms) return false;
        }
        frame_=next;
        return true;
    }

    void render(uint64_t now) {
        static const unsigned fields[2][6]={
            {DPF_B1_DELTA,DPF_B1_INLET_PRESSURE,DPF_B1_OUTLET_PRESSURE,DPF_B1_INLET_TEMP,DPF_B1_OUTLET_TEMP,DPF_COOLANT},
            {DPF_B2_DELTA,DPF_B2_INLET_PRESSURE,DPF_B2_OUTLET_PRESSURE,DPF_B2_INLET_TEMP,DPF_B2_OUTLET_TEMP,DPF_COOLANT}
        };
        unsigned valid=0,unsupported=0,stale=0;
        const uint64_t elapsed=frame_.sent_ms && now>=frame_.sent_ms ? now-frame_.sent_ms : static_cast<uint64_t>(DPF_STALE_MS);
        for (unsigned slot=0;slot<6;++slot) {
            const unsigned field=fields[bank_][slot];
            const int state=dpf_measurement_state(&frame_,field,elapsed);
            char value[96];
            if (state==DPF_VALID) {
                const char *unit=dpf_unit(field,metric_);
                double display=dpf_display_value(field,frame_.value[field],metric_);
                if (display==0) display=0; /* canonical positive zero */
                snprintf(value,sizeof value,"%.*f %s%s",field<DPF_B1_INLET_TEMP && !metric_ ? 3 : 1,display,
                         field>=DPF_B1_INLET_TEMP ? "\260" : "",unit);
                if (slot<5) ++valid;
            } else {
                snprintf(value,sizeof value,"%s",state==DPF_UNSUPPORTED ? "Unsupported" :
                         state==DPF_UNAVAILABLE ? "Unavailable" : state==DPF_STALE ? "Stale" : "Unknown");
                if (slot<5 && state==DPF_UNSUPPORTED) ++unsupported;
                if (slot<5 && state==DPF_STALE) ++stale;
            }
            text(slot,value);
        }
        text(6,save_error_ ? "Could not save settings" : socket_<0 ? "Telemetry unavailable" :
             !visible_ || stopping_ ? "Monitoring stopped" : valid==5 ? "Live DPF readings" :
             valid ? "Some DPF readings unavailable" : stale ? "DPF readings stale" :
             unsupported==5 ? "DPF readings unsupported" : "Waiting for DPF readings");
        char caption[96];
        snprintf(caption,sizeof caption,"Bank %u",bank_+1);text(7,caption);
        snprintf(caption,sizeof caption,"Bank %u / %s, %s",bank_+1,metric_ ? "mbar" : "psi",metric_ ? "C" : "F");
        text(8,caption);text(9,save_error_ ? "Could not save settings" : "");
        const int progress=dpf_measurement_state(&frame_,DPF_REGEN_PROGRESS,elapsed);
        const int type=dpf_measurement_state(&frame_,DPF_REGEN_TYPE,elapsed);
        const char *state=!regen_ ? "Not shown" : progress==DPF_VALID ?
            frame_.value[DPF_REGEN_PROGRESS]==0 ? "Not in progress" :
            type==DPF_VALID ? frame_.value[DPF_REGEN_TYPE]==1 ? "Active" : "Passive" : "In progress" :
            progress==DPF_UNSUPPORTED ? "Unsupported" : progress==DPF_UNAVAILABLE ? "Unavailable" :
            progress==DPF_STALE ? "Stale" : "Unknown";
        snprintf(caption,sizeof caption,"Regeneration: %s",state);text(10,caption);
        text(11,regen_ ? "Regeneration status: shown" : "Regeneration status: hidden");
    }

    void tick() {
        reap();
        if (!visible_) { stopWorker();render(bd_mono_ms());return; }
        const uint64_t now=bd_mono_ms();
        if (socket_<0 && now>=bind_retry_) bindSocket(now);
        if (socket_>=0 && child_<=0 && now>=retry_) {
            const DiagnosticGateway gateway(dpfGateway);
            token_=(now<<16)|(++dpfLaunchSerial & 0xffffu);
            if (!token_) token_=1;
            char token[24];snprintf(token,sizeof token,"%llu",static_cast<unsigned long long>(token_));
            const char *args[]={dpfWorker,"--host",gateway.host(),"--token",token,regen_ ? "--regen-status" : 0,0};
            clearFrame();opened_=now;
            child_=launch(args,-1);retry_=now+2000;
            if (child_<=0) token_=0;
        }
        for (unsigned i=0;socket_>=0 && i<8;++i) {
            char packet[DPF_MAX_PACKET+1];
            sockaddr_in peer;socklen_t peer_size=sizeof peer;
            const ssize_t size=recvfrom(socket_,packet,sizeof packet,0,reinterpret_cast<sockaddr*>(&peer),&peer_size);
            if (size<0) break;
            if (size>0 && size<DPF_MAX_PACKET && peer_size==sizeof peer)
                accept(packet,static_cast<size_t>(size),peer,bd_mono_ms());
        }
        render(bd_mono_ms());
    }

    void bindSocket(uint64_t now) {
        socket_=gauge_bind(DPF_PORT);
        if (socket_>=0 && fcntl(socket_,F_SETFD,FD_CLOEXEC)<0) { close(socket_);socket_=-1; }
        bind_retry_=now+2000;
    }

public:
    DPFDeck(bmw::IDispatcher &d,bmw::BasicString<char> name,unsigned major,unsigned minor,
            bmw::RHMIInterfaceVersion version,const bmw::Bundle &config,const bmw::Bundle &resources,
            const char *preferences=dpfPreferences)
        : PluginApp(d,name,major,minor,version,config,resources),preferences_(preferences),socket_(-1),child_(-1),
          generation_(0),visible_(0),bank_(0),labels_known_(0),metric_(true),regen_(false),stopping_(false),save_error_(false),token_(0),opened_(0),retry_(0),bind_retry_(0) {
        clearFrame();memset(labels_,0,sizeof labels_);
    }
    virtual ~DPFDeck() { shutdown(); }
    virtual bool onFinalize() { shutdown();return true; }
    virtual bool onInit() {
        shutdown();loadPreferences();clearFrame();memset(labels_,0,sizeof labels_);labels_known_=0;
        bindSocket(bd_mono_ms());
        typedef bmw::Delegate<bool,const bmw::Parcel&> Handler;
        Handler action;action.m_obj=this;
        action.m_exec=&Handler::execImpl<DPFDeck,&DPFDeck::visibility<0> >;addHandler(100u,11u,action);
        action.m_exec=&Handler::execImpl<DPFDeck,&DPFDeck::visibility<1> >;addHandler(101u,11u,action);
        action.m_exec=&Handler::execImpl<DPFDeck,&DPFDeck::bank<0> >;addHandler(420u,action);
        action.m_exec=&Handler::execImpl<DPFDeck,&DPFDeck::bank<1> >;addHandler(421u,action);
        action.m_exec=&Handler::execImpl<DPFDeck,&DPFDeck::units<true> >;addHandler(430u,action);
        action.m_exec=&Handler::execImpl<DPFDeck,&DPFDeck::units<false> >;addHandler(431u,action);
        action.m_exec=&Handler::execImpl<DPFDeck,&DPFDeck::regenStatus<true> >;addHandler(440u,action);
        action.m_exec=&Handler::execImpl<DPFDeck,&DPFDeck::regenStatus<false> >;addHandler(441u,action);
        render(bd_mono_ms());
        postPoll();
        return true;
    }
    virtual bool invokeDataUpdate(unsigned key,const bmw::Parcel &data) {
        if (key!=dpfPollKey) return PluginApp::invokeDataUpdate(key,data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation==generation_) { tick();postPoll(); }
        return true;
    }
};

extern "C" {
PluginAppInfo pluginAppInfo={1,"DPFDeck","iDriveDeck",1,create_app<DPFDeck>,destroy_app};
}
