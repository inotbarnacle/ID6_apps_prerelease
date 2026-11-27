#ifndef ID6_N57_DASHBOARD_H
#define ID6_N57_DASHBOARD_H

#include "plugin_factory.h"
#include "hmi_state_activity.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include "n57_dashboard_values.h"
#include "n57.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>

/* PowerDeck lifecycle. Diagnostic requests
 * remain in the existing collector, including its identity check and lease. */
class N57Dashboard : public bmw::PluginApp {
    const char* worker_;
    const char* gateway_;
    unsigned port_;
    bd_mask mask_;
    int socket_;
    pid_t child_;
    bool stopping_;
    bool collection_enabled_;
    bool unfocused_collection_;
    unsigned visible_states_;
    unsigned shown_states_,focused_states_,valid_states_;
    unsigned collecting_states_;
    unsigned generation_;
    uint64_t retry_at_;
    unsigned images_[128];
    char status_[96];

    void reap() {
        if (child_<=0) return;
        pid_t ended=waitpid(child_,0,WNOHANG);
        if (ended==child_ || (ended<0 && errno==ECHILD && kill(child_,0)!=0 && errno==ESRCH)) { child_=-1; stopping_=false; }
    }
    void stopChild() {
        reap();
        if (child_>0 && !stopping_) { kill(child_,SIGTERM); stopping_=true; }
    }
    void shutdown() {
        visible_=false;
        visible_states_=0;
        shown_states_=focused_states_=valid_states_=0;
        unfocused_collection_=false;
        collecting_states_=0;
        ++generation_;
        stopChild();
        if (socket_>=0) { close(socket_); socket_=-1; }
        received_=0;
        memset(&frame_,0,sizeof frame_);
    }
    void startChild() {
        if (!collection_enabled_ || !visible_ || !collecting_states_ || child_>0 || socket_<0 || bd_mono_ms()<retry_at_) return;
        unsigned index=0;
        retry_at_=bd_mono_ms()+2000;
        if (n57_settings(&index)!=1) return;
        const DiagnosticGateway gateway(gateway_);
        char selected[24], pin[9], port[6];
        snprintf(selected,sizeof selected,"%llu",static_cast<unsigned long long>(mask_));
        snprintf(pin,sizeof pin,"%06X",index);
        snprintf(port,sizeof port,"%u",port_);
        const char* args[]={worker_,"--host",gateway.host(),"--session","--diesel",
                            "--n57-sgbd",pin,"--signals",selected,"--udp-port",port,0};
        child_=launch(args,-1);
    }
    void postPoll() {
        bmw::Parcel data;
        data.writeUInt32(0x4e353744u);
        data.writeUInt32(generation_);
        post(bmw::Message(0x40000003u,data),visible_ ? 100u : 250u);
    }
    template <unsigned Key> bool stateActivity(const bmw::Map<unsigned,bmw::Variant>& event) {
        setViewVisible(1u,viewActivity(1u,Key,event));
        return true;
    }

protected:
    bd_frame frame_;
    uint64_t received_;
    bool visible_;

    virtual void render(uint64_t now)=0;
    virtual void packetReceived(const bd_frame&) {}
    virtual void viewChanged(bool) {}
    bool collectionEnabled() const { return collection_enabled_; }
    void setUnfocusedCollection(bool enabled) { unfocused_collection_=enabled; }
    bool viewShown(unsigned bit) const { return (shown_states_&valid_states_&bit)!=0; }
    bool viewActive(unsigned bit) const {
        return viewShown(bit) && ((focused_states_&bit)!=0 || unfocused_collection_);
    }
    void setCollectionEnabled(bool enabled) {
        if (enabled==collection_enabled_) return;
        collection_enabled_=enabled;
        received_=0;
        memset(&frame_,0,sizeof frame_);
        if (enabled) retry_at_=0;
        else stopChild();
        render(bd_mono_ms());
    }
    bool viewActivity(unsigned bit,unsigned key,const bmw::Map<unsigned,bmw::Variant>& event) {
        bool value=false;
        if (hmiStateBool(event,key,value)) valid_states_|=bit;
        else valid_states_&=~bit;
        hmiStateActivity(shown_states_,focused_states_,bit,key,event);
        return viewActive(bit);
    }
    void setViewVisible(unsigned bit,bool show,bool collect=true) {
        const unsigned before=collecting_states_;
        if (show) visible_states_|=bit;
        else visible_states_&=~bit;
        if (show && collect) collecting_states_|=bit;
        else collecting_states_&=~bit;
        const bool active=visible_states_!=0;
        if (active==visible_ && (before!=0)==(collecting_states_!=0)) return;
        visible_=active;
        received_=0;
        memset(&frame_,0,sizeof frame_);
        if (collecting_states_) { retry_at_=0; startChild(); }
        else stopChild();
        viewChanged(active);
        render(bd_mono_ms());
    }
    bool available(unsigned signal,uint64_t now,double& value) const {
        return visible_ && received_ && n57DashboardValue(frame_,signal,now,value);
    }
    void label(const char* text) {
        if (strcmp(status_,text)) {
            updateModel(50u,bmw::BasicString<char>(text));
            snprintf(status_,sizeof status_,"%s",text);
        }
    }
    void image(unsigned model,unsigned asset) {
        if (model>=sizeof images_/sizeof images_[0] || images_[model]!=asset) {
            updateModel(model,bmw::ImageID(asset));
            if (model<sizeof images_/sizeof images_[0]) images_[model]=asset;
        }
    }
    void digits(unsigned model,unsigned count,const char* text,unsigned base) {
        const unsigned length=static_cast<unsigned>(strlen(text));
        const unsigned padding=length<count ? (count-length)/2 : 0;
        for (unsigned i=0;i<count;++i) {
            const char c=i>=padding && i-padding<length ? text[i-padding] : ' ';
            const unsigned glyph=c>='0' && c<='9' ? c-'0' : c=='-' ? 10u : c=='.' ? 12u : 11u;
            image(model+i,base+glyph);
        }
    }

public:
    N57Dashboard(bmw::IDispatcher& dispatcher,bmw::BasicString<char> name,
                  unsigned major,unsigned minor,bmw::RHMIInterfaceVersion version,
                  const bmw::Bundle& config,const bmw::Bundle& resources,
                  const char* worker,const char* gateway,unsigned port,bd_mask mask)
        : PluginApp(dispatcher,name,major,minor,version,config,resources),worker_(worker),
          gateway_(gateway),port_(port),mask_(mask),socket_(-1),child_(-1),stopping_(false),collection_enabled_(true),unfocused_collection_(false),visible_states_(0),shown_states_(0),focused_states_(0),valid_states_(0),collecting_states_(0),
          generation_(0),retry_at_(0),received_(0),visible_(false) {
        memset(&frame_,0,sizeof frame_);
        memset(images_,0,sizeof images_);
        memset(status_,0,sizeof status_);
    }
    virtual ~N57Dashboard() { shutdown(); }
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool onInit() {
        shutdown();
        collection_enabled_=true;
        memset(images_,0,sizeof images_);
        memset(status_,0,sizeof status_);
        socket_=socket(AF_INET,SOCK_DGRAM,0);
        if (socket_>=0) {
            struct sockaddr_in address;
            memset(&address,0,sizeof address);
            address.sin_family=AF_INET;
            address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            address.sin_port=htons(port_);
            if (fcntl(socket_,F_SETFL,O_NONBLOCK)<0 || fcntl(socket_,F_SETFD,FD_CLOEXEC)<0 ||
                bind(socket_,reinterpret_cast<struct sockaddr*>(&address),sizeof address)<0) {
                close(socket_); socket_=-1;
            }
        }
        typedef bmw::Delegate<bool,const bmw::Map<unsigned,bmw::Variant>&> StateAction;
        StateAction state;state.m_obj=this;
        state.m_exec=&StateAction::execImpl<N57Dashboard,&N57Dashboard::stateActivity<23> >;
        addHandlerWithMap(100u,11u,state);
        state.m_exec=&StateAction::execImpl<N57Dashboard,&N57Dashboard::stateActivity<4> >;
        addHandlerWithMap(100u,1u,state);
        render(bd_mono_ms());
        if (socket_>=0) postPoll();
        return true;
    }
    virtual bool invokeDataUpdate(unsigned key,const bmw::Parcel& data) {
        if (key!=0x4e353744u) return PluginApp::invokeDataUpdate(key,data);
        unsigned generation;
        if (!data.readUInt32(generation)) return false;
        if (generation!=generation_ || socket_<0) return true;
        reap();
        startChild();
        char packet[BD_MAX_PACKET];
        for (unsigned i=0;i<8;++i) {
            const ssize_t length=recv(socket_,packet,sizeof packet,0);
            if (length<=0) break;
            bd_frame next;
            if (collection_enabled_ && visible_ && collecting_states_ && !stopping_ && n57DashboardPacket(packet,static_cast<size_t>(length),
                                                            bd_mono_ms(),received_,next)) {
                frame_=next;
                received_=next.sent_ms;
                packetReceived(next);
            }
        }
        render(bd_mono_ms());
        postPoll();
        return true;
    }
};

#endif
