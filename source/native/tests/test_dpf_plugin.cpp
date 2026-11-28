#include "fake_bmw.h"
#include "diagnostic_worker.h"
#include "gauge_core.h"
#include <assert.h>
#include <poll.h>
#include <sys/wait.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
static uint64_t clock_ms=10000;
extern "C" uint64_t bd_mono_ms(void) { return clock_ms; }
static std::vector<std::string> launched;
static unsigned launches,terms;
static int forced_wait_error,forced_probe_error;
static bool fail_launch;

static pid_t capture_launch(const char *const *args,int output) {
    assert(output==-1);launched.clear();++launches;
    for (unsigned i=0;args[i];++i) launched.push_back(args[i]);
    if (fail_launch) return -1;
    const pid_t parent=getpid(),child=fork();assert(child>=0);
    if (!child) {
        // This stand-in never executes diagnostics and holds no inherited sockets.
        for (int fd=3;fd<256;++fd) close(fd);
        while (getppid()==parent) poll(0,0,5);
        _exit(0);
    }
    return child;
}
static pid_t wrapped_waitpid(pid_t pid,int *status,int flags) {
    if (forced_wait_error) { errno=forced_wait_error;return -1; }
    return waitpid(pid,status,flags);
}
static int wrapped_kill(pid_t pid,int signal) {
    if (!signal && forced_probe_error) { errno=forced_probe_error;return -1; }
    if (signal==SIGTERM) ++terms;
    return kill(pid,signal);
}
#define launch capture_launch
#define waitpid wrapped_waitpid
#define kill wrapped_kill
#include "../src/plugins/dpf_deck.cpp"
#undef kill
#undef waitpid
#undef launch

static FakeUpdate last(unsigned model) {
    for (std::vector<FakeUpdate>::reverse_iterator i=fake_updates.rbegin();i!=fake_updates.rend();++i)
        if (i->model==model) return *i;
    assert(!"model was not updated");return FakeUpdate();
}
static void capture_models(const char *name) {
    const char *directory=getenv("DPF_MODEL_CAPTURE_DIR");
    if (!directory) return;
    char path[512];const int size=snprintf(path,sizeof path,"%s/%s.tsv",directory,name);
    assert(size>0 && static_cast<size_t>(size)<sizeof path);
    FILE *file=fopen(path,"wb");assert(file);
    for (unsigned model=50;model<62;++model) fprintf(file,"%u\t%s\n",model,last(model).text.c_str());
    assert(!ferror(file) && fclose(file)==0);
}
static void poll_app(DPFDeck &app) {
    bmw::Parcel event;event.writeUInt32(app.generation_);
    assert(app.invokeDataUpdate(dpfPollKey,event));
}
static void shown(unsigned state,bool value) {
    bmw::Parcel event(value);assert(fake_fire(state,11,event));
}
static void action(unsigned id) { bmw::Parcel event;assert(fake_fire(0,id,event)); }
static dpf_frame sample(const DPFDeck &app,uint32_t sequence) {
    dpf_frame f;memset(&f,0,sizeof f);
    f.seq=sequence;f.worker_pid=static_cast<uint32_t>(app.child_);f.token=app.token_;f.sent_ms=clock_ms;
    f.known=f.supported=f.valid=DPF_ALL;
    const double values[]={-.1,0,100,-327.68,0,655.35,0,.1,1800,60,82};
    memcpy(f.value,values,sizeof values);
    return f;
}
static void send_bytes(int source,const char *data,size_t size) {
    const sockaddr_in peer=gauge_local_address(DPF_PORT);
    assert(sendto(source,data,size,0,reinterpret_cast<const sockaddr*>(&peer),sizeof peer)==static_cast<ssize_t>(size));
}
static void send_frame(DPFDeck &app,int source,const dpf_frame &f) {
    char packet[DPF_MAX_PACKET];const int n=dpf_encode(&f,packet,sizeof packet);assert(n>0);
    send_bytes(source,packet,static_cast<size_t>(n));poll_app(app);
}
static void reap(DPFDeck &app) {
    forced_wait_error=forced_probe_error=0;
    for (unsigned i=0;i<200 && app.child_>0;++i) { app.reap();if (app.child_>0) poll(0,0,1); }
    assert(app.child_<0);
}

int main() {
    char directory[]="/tmp/dpf-plugin.XXXXXX";assert(mkdtemp(directory));
    char prefs[256],badprefs[256];
    snprintf(prefs,sizeof prefs,"%s/DPFDeck.prefs",directory);
    snprintf(badprefs,sizeof badprefs,"%s/missing/prefs",directory);
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    int source=gauge_bind(DPF_ACTIVE_PORT);assert(source>=0);
    fake_reset();
    {
        DPFDeck app(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(app.onInit() && app.socket_>=0 && launches==0);
        assert(fcntl(app.socket_,F_GETFD)&FD_CLOEXEC);
        assert(last(50).text=="Unknown" && last(56).text=="Monitoring stopped");
        assert(last(59).text.empty() && !app.regen_);
        assert(last(60).text=="Regeneration: Not shown" && last(61).text=="Regeneration status: hidden");
        shown(100,true);poll_app(app);
        assert(app.child_>0 && launches==1 && app.token_);
        assert(launched.size()==5 && launched[0]==dpfWorker && launched[1]=="--host" && launched[3]=="--token");
        assert(strtoull(launched[4].c_str(),0,10)==app.token_);
        const pid_t child=app.child_;
        const uint64_t token=app.token_;
        dpf_frame frame=sample(app,1);send_frame(app,source,frame);
        assert(last(50).text=="-1.0 mbar" && last(51).text=="0.0 mbar");
        assert(last(53).text=="0.0 \260C" && last(55).text=="82.0 \260C");
        assert(last(56).text=="Live DPF readings");
        capture_models("bank1-live");
        shown(100,false);shown(101,true);poll_app(app);
        assert(app.child_==child && app.token_==token && !app.stopping_ && launches==1 && terms==0);
        assert(last(50).text=="-1.0 mbar");
        action(421);assert(app.bank_==1 && last(50).text=="-3276.8 mbar");
        capture_models("bank2-live");
        action(431);assert(!app.metric_ && last(50).text=="-47.526 psi");
        assert(last(55).text=="179.6 \260F");
        action(420);assert(last(50).text=="-0.015 psi");
        action(430);
        shown(101,false);shown(100,true);poll_app(app);
        assert(app.child_==child && launches==1);

        // Wrong owner, generation, time, wire format and source cannot advance state.
        const uint32_t accepted=app.frame_.seq;
        dpf_frame wrong=sample(app,2);++wrong.token;send_frame(app,source,wrong);
        wrong=sample(app,2);++wrong.worker_pid;send_frame(app,source,wrong);
        wrong=sample(app,2);wrong.sent_ms=clock_ms+1;send_frame(app,source,wrong);
        wrong=sample(app,2);wrong.sent_ms=app.opened_-1;send_frame(app,source,wrong);
        wrong=sample(app,2);wrong.sent_ms=clock_ms-DPF_STALE_MS;send_frame(app,source,wrong);
        wrong=sample(app,1);send_frame(app,source,wrong); // duplicate sequence
        int other=socket(AF_INET,SOCK_DGRAM,0);assert(other>=0);
        wrong=sample(app,2);send_frame(app,other,wrong);close(other);
        char oversized[DPF_MAX_PACKET+1];memset(oversized,'X',sizeof oversized);
        send_bytes(source,oversized,sizeof oversized);poll_app(app);
        const char old_packet[]="NBG8 1 0 0 1\n";
        send_bytes(source,old_packet,sizeof old_packet-1);poll_app(app);
        assert(app.frame_.seq==accepted && last(50).text=="-1.0 mbar");
        sockaddr_in peer=gauge_local_address(DPF_ACTIVE_PORT);
        peer.sin_addr.s_addr=htonl(0x7f000002u);
        char packet[DPF_MAX_PACKET];int size=dpf_encode(&wrong,packet,sizeof packet);assert(size>0);
        assert(!app.accept(packet,static_cast<size_t>(size),peer,clock_ms));

        frame=sample(app,2);frame.known&=~1u;frame.supported&=~3u;frame.valid&=~7u;
        send_frame(app,source,frame);
        assert(last(50).text=="Unknown" && last(51).text=="Unsupported" && last(52).text=="Unavailable");
        capture_models("availability");
        frame=sample(app,3);frame.age_ms[0]=DPF_STALE_MS;send_frame(app,source,frame);
        assert(last(50).text=="Stale" && last(51).text=="0.0 mbar");
        clock_ms+=DPF_STALE_MS;poll_app(app);
        assert(last(50).text=="Stale" && last(56).text=="DPF readings stale");
        capture_models("stale");
        frame=sample(app,4);send_frame(app,source,frame);
        assert(last(50).text=="-1.0 mbar");
        frame=sample(app,5);--frame.sent_ms;send_frame(app,source,frame);
        assert(app.frame_.seq==4); // higher sequence cannot regress sender time

        // The per-tick UDP drain remains bounded even with queued valid frames.
        for (unsigned i=5;i<25;++i) {
            frame=sample(app,i);size=dpf_encode(&frame,packet,sizeof packet);assert(size>0);
            send_bytes(source,packet,static_cast<size_t>(size));
        }
        poll_app(app);assert(app.frame_.seq==12);
        poll_app(app);assert(app.frame_.seq==20);
        poll_app(app);assert(app.frame_.seq==24);
        app.frame_.seq=UINT32_MAX-1;
        frame=sample(app,UINT32_MAX);send_frame(app,source,frame);assert(app.frame_.seq==UINT32_MAX);
        frame=sample(app,1);send_frame(app,source,frame);assert(app.frame_.seq==1);
        frame=sample(app,UINT32_MAX);send_frame(app,source,frame);assert(app.frame_.seq==1);

        // Foreign reapers and inconclusive process probes cannot duplicate the worker.
        forced_wait_error=ECHILD;poll_app(app);
        assert(app.child_==child && launches==1);
        forced_probe_error=EPERM;clock_ms+=3000;poll_app(app);
        assert(app.child_==child && launches==1);
        forced_wait_error=EINTR;poll_app(app);assert(app.child_==child && launches==1);
        shown(100,false);poll_app(app);
        assert(app.stopping_ && terms==1 && last(56).text=="Monitoring stopped");
        frame=sample(app,25);send_frame(app,source,frame);
        assert(!app.frame_.sent_ms);
        forced_wait_error=forced_probe_error=0;reap(app);
        clock_ms+=1;shown(100,true);poll_app(app);
        assert(app.child_>0 && app.token_!=token && launches==2);
        const unsigned generation=app.generation_;
        assert(app.onFinalize());reap(app);
        bmw::Parcel old;old.writeUInt32(generation);
        const size_t posts=fake_posts.size();assert(app.invokeDataUpdate(dpfPollKey,old));
        assert(app.socket_<0 && fake_posts.size()==posts && launches==2);
    }

    // Changing the optional request set retains a retiring process until its
    // exit is established. A stale generation cannot populate the new display.
    fake_reset();
    {
        DPFDeck app(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(app.onInit() && !app.regen_);shown(100,true);poll_app(app);
        const pid_t child=app.child_;
        const uint64_t token=app.token_;
        const unsigned before_launches=launches,before_terms=terms;
        dpf_frame old=sample(app,1);send_frame(app,source,old);
        forced_wait_error=ECHILD;forced_probe_error=EPERM;
        action(440);
        assert(app.regen_ && app.stopping_ && app.child_==child && terms==before_terms+1);
        action(440);poll_app(app);send_frame(app,source,old);
        assert(app.child_==child && launches==before_launches && terms==before_terms+1 && !app.frame_.sent_ms);
        reap(app);++clock_ms;poll_app(app);
        assert(app.child_>0 && app.token_!=token && launches==before_launches+1);
        assert(launched.size()==6 && launched[5]=="--regen-status");
        send_frame(app,source,old);assert(!app.frame_.sent_ms);
        assert(last(60).text=="Regeneration: Unknown");
        const pid_t enabled=app.child_;
        const uint64_t enabled_token=app.token_;
        shown(100,false);shown(101,true);action(421);action(431);poll_app(app);
        assert(app.child_==enabled && app.token_==enabled_token && !app.stopping_ && app.regen_);
        shown(101,false);shown(100,true);
        action(420);action(430);
        dpf_frame f=sample(app,1);
        f.value[DPF_REGEN_TYPE]=1;send_frame(app,source,f);
        assert(last(60).text=="Regeneration: Not in progress");
        capture_models("regen-inactive");
        f.seq++;f.value[DPF_REGEN_PROGRESS]=1;send_frame(app,source,f);
        assert(last(60).text=="Regeneration: Active");capture_models("regen-active");
        f.seq++;f.value[DPF_REGEN_TYPE]=0;send_frame(app,source,f);
        assert(last(60).text=="Regeneration: Passive");capture_models("regen-passive");
        f.seq++;f.valid&=~DPF_BIT(DPF_REGEN_TYPE);send_frame(app,source,f);
        assert(last(60).text=="Regeneration: In progress");
        f=sample(app,5);f.value[DPF_REGEN_PROGRESS]=1;f.age_ms[DPF_REGEN_TYPE]=DPF_STALE_MS;
        send_frame(app,source,f);assert(last(60).text=="Regeneration: In progress");
        f=sample(app,6);f.age_ms[DPF_REGEN_PROGRESS]=DPF_STALE_MS;send_frame(app,source,f);
        assert(last(60).text=="Regeneration: Stale");capture_models("regen-stale");
        f=sample(app,7);f.valid&=~DPF_BIT(DPF_REGEN_PROGRESS);send_frame(app,source,f);
        assert(last(60).text=="Regeneration: Unavailable");
        f=sample(app,8);f.valid&=~DPF_BIT(DPF_REGEN_PROGRESS);f.supported=f.valid;send_frame(app,source,f);
        assert(last(60).text=="Regeneration: Unsupported");
        f=sample(app,9);f.valid&=~DPF_BIT(DPF_REGEN_PROGRESS);f.supported=f.known=f.valid;send_frame(app,source,f);
        assert(last(60).text=="Regeneration: Unknown"); // type alone cannot assert progress
        app.preferences_=badprefs;action(441);
        assert(app.regen_ && app.child_==enabled && !app.stopping_ && terms==before_terms+1);
        app.preferences_=prefs;action(441);
        assert(!app.regen_ && app.stopping_ && terms==before_terms+2);
        reap(app);++clock_ms;poll_app(app);
        assert(app.child_>0 && launched.size()==5 && app.token_!=enabled_token);
        assert(last(60).text=="Regeneration: Not shown");
        assert(app.onFinalize());reap(app);
    }

    fake_reset();
    {
        DPFDeck gone(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(gone.onInit());shown(100,true);poll_app(gone);
        const pid_t child=gone.child_;assert(child>0);
        assert(kill(child,SIGTERM)==0 && waitpid(child,0,0)==child);
        forced_wait_error=ECHILD;forced_probe_error=ESRCH;
        gone.reap();assert(gone.child_<0 && !gone.token_ && !gone.frame_.sent_ms);
        forced_wait_error=forced_probe_error=0;
        assert(gone.onFinalize());
    }
    const unsigned char bad[][5]={{'D',2,1,1,0},{'D',1,2,1,0},{'D',1,1,2,0},{'G',1,1,1,0}};
    for (unsigned i=0;i<sizeof bad/sizeof bad[0];++i) {
        assert(savePersistentBytes(prefs,bad[i],4));fake_reset();
        DPFDeck invalid(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(invalid.onInit() && invalid.bank_==0 && invalid.metric_ && invalid.onFinalize());
    }
    for (unsigned length=3;length<=5;length+=2) {
        const unsigned char bytes[]={'D',1,1,0,0};assert(savePersistentBytes(prefs,bytes,length));fake_reset();
        DPFDeck invalid(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(invalid.onInit() && invalid.bank_==0 && invalid.metric_ && invalid.onFinalize());
    }
    const unsigned char bad_v2[][6]={{'D',2,2,1,0,0},{'D',2,1,2,0,0},{'D',2,1,1,2,0},
                                    {'D',3,1,1,1,0},{'G',2,1,1,1,0},{'D',1,1,1,1,0}};
    for (unsigned i=0;i<sizeof bad_v2/sizeof bad_v2[0];++i) {
        assert(savePersistentBytes(prefs,bad_v2[i],5));fake_reset();
        DPFDeck invalid(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(invalid.onInit() && invalid.bank_==0 && invalid.metric_ && !invalid.regen_ && invalid.onFinalize());
    }
    const unsigned char v2[]={'D',2,1,0,1,0};
    for (unsigned length=4;length<=6;length+=2) {
        assert(savePersistentBytes(prefs,v2,length));fake_reset();
        DPFDeck invalid(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(invalid.onInit() && invalid.bank_==0 && invalid.metric_ && !invalid.regen_ && invalid.onFinalize());
    }
    for (unsigned record=1;record<=2;++record) {
        const unsigned char bytes[]={'D',static_cast<unsigned char>(record),1,0,1};
        assert(savePersistentBytes(prefs,bytes,record==1 ? 4 : 5));fake_reset();
        DPFDeck restored(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(restored.onInit() && restored.bank_==1 && !restored.metric_ && restored.regen_==(record==2));
        assert(restored.onFinalize());
    }
    unlink(prefs);
    fake_reset();
    {
        DPFDeck restored(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(restored.onInit() && restored.bank_==0 && restored.metric_);
        action(421);action(431);assert(restored.bank_==1 && !restored.metric_);
        assert(restored.onFinalize());
    }
    fake_reset();
    {
        DPFDeck restored(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(restored.onInit() && restored.bank_==1 && !restored.metric_ && restored.onFinalize());
    }
    fake_reset();
    {
        DPFDeck failed(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,badprefs);
        assert(failed.onInit());action(421);
        assert(failed.bank_==0 && failed.metric_ && last(56).text=="Could not save settings");
        failed.preferences_=prefs;
        assert(failed.onInit() && last(59).text.empty() && !failed.save_error_);
        assert(failed.onFinalize());
    }
    // A busy telemetry port never launches a child and recovers without an HMI restart.
    int owner=gauge_bind(DPF_PORT);assert(owner>=0);
    fake_reset();
    {
        DPFDeck busy(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        const unsigned before=launches;
        assert(busy.onInit() && busy.socket_<0);
        shown(100,true);poll_app(busy);assert(launches==before && last(56).text=="Telemetry unavailable");
        close(owner);clock_ms+=2001;poll_app(busy);
        assert(busy.socket_>=0 && busy.child_>0 && launches==before+1);
        assert(busy.onFinalize());reap(busy);
    }
    // Failed launch retries stay bounded and do not retain a usable token.
    fake_reset();fail_launch=true;
    {
        DPFDeck failed(dispatcher,bmw::BasicString<char>("DPFDeck"),1,0,version,bundle,bundle,prefs);
        assert(failed.onInit());shown(100,true);const unsigned before=launches;poll_app(failed);
        assert(launches==before+1 && !failed.token_);
        for (unsigned i=0;i<20;++i) poll_app(failed);
        assert(launches==before+1);
        clock_ms+=2000;poll_app(failed);assert(launches==before+2);
        assert(failed.onFinalize());
    }
    fail_launch=false;close(source);unlink(prefs);rmdir(directory);
    puts("DPF production callbacks/UDP, ownership, generations, bounded drains/retries, states, units, persistence and child cleanup passed");
}
