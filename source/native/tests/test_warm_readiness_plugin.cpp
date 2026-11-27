#include "fake_bmw.h"
#include "gauge_core.h"
#include "diagnostic_worker.h"
#include <assert.h>
#include <math.h>

namespace bmw { class IDispatcher {}; class Bundle {}; }
static uint64_t clock_ms=1000;
extern "C" uint64_t bd_mono_ms(void) { return clock_ms; }
static unsigned launch_attempts=0;
static pid_t no_worker(const char* const* args,int) {
    assert(args && args[0]); ++launch_attempts; return -1;
}
#define launch no_worker
#include "../src/plugins/warm_deck.cpp"
#undef launch

static FakeUpdate last(unsigned model) {
    for (std::vector<FakeUpdate>::reverse_iterator i=fake_updates.rbegin();i!=fake_updates.rend();++i)
        if (i->model==model) return *i;
    assert(!"model not updated"); return FakeUpdate();
}
static void visible(bool value) { bmw::Parcel event(value); assert(fake_fire(100,11,event)); }
static void send_bytes(WarmDeck& app,int source,const void* bytes,size_t size) {
    const sockaddr_in destination=gauge_local_address(WARM_PORT);
    assert(sendto(source,bytes,size,0,reinterpret_cast<const sockaddr*>(&destination),sizeof destination)==static_cast<ssize_t>(size));
    app.tick();
    assert(app.child_<0); // launch is intercepted; no diagnostic process runs
}
static void send_frame(WarmDeck& app,int source,const warm_frame& frame) { send_bytes(app,source,&frame,sizeof frame); }
static void fresh(warm_frame& frame,double oil,double coolant,double trans) {
    for (unsigned i=0;i<WARM_COUNT;++i) frame.sample[i]=clock_ms;
    frame.celsius[0]=oil; frame.celsius[1]=coolant; frame.celsius[2]=trans;
}
static void advisory() {
    assert(last(54).text.find("advisory")!=std::string::npos);
    assert(last(53).text.find("RPM")==std::string::npos && last(54).text.find("RPM")==std::string::npos);
}

int main() {
    bmw::IDispatcher dispatcher; bmw::Bundle bundle; bmw::RHMIInterfaceVersion version={1};
    fake_reset();
    WarmDeck app(dispatcher,bmw::BasicString<char>("WarmDeck"),1,0,version,bundle,bundle);
    assert(app.onInit() && app.socket_>=0);
    int source=gauge_bind(WARM_ACTIVE_PORT); assert(source>=0);
    int wrong_source=socket(AF_INET,SOCK_DGRAM,0); assert(wrong_source>=0);
    visible(true);
    assert(last(50).text=="--" && last(55).text.find("unknown")!=std::string::npos);
    assert(last(60).number==58100); advisory();

    warm_frame frame={{0,1000,1000},{0,75,40}};
    send_frame(app,source,frame);
    assert(last(50).text=="--" && last(51).text=="75.0 \260C" && last(52).text=="40.0 \260C");
    assert(last(55).text.find("unknown")!=std::string::npos);
    assert(last(56).text.find("met")!=std::string::npos && last(57).text.find("met")!=std::string::npos);
    assert(last(61).number==58120 && last(62).number==58120);
    assert(last(53).text=="Waiting for all three temperatures"); advisory();

    clock_ms=2000; fresh(frame,40,73,37);
    send_frame(app,source,frame);
    assert(last(55).text.find("pending")!=std::string::npos);
    assert(last(56).text.find("met")!=std::string::npos && last(57).text.find("met")!=std::string::npos);
    // Older positive samples cannot replace newer samples in another fluid.
    clock_ms=2500;
    frame.sample[0]=1500; frame.celsius[0]=90;
    frame.sample[1]=2500; frame.celsius[1]=69;
    send_frame(app,source,frame);
    assert(last(50).text=="40.0 \260C" && last(51).text=="69.0 \260C");
    assert(last(56).text.find("pending")!=std::string::npos);
    // A zero marker invalidates immediately; queued old positives cannot revive it.
    frame.sample[0]=0; send_frame(app,source,frame); assert(last(50).text=="--");
    frame.sample[0]=2000; frame.celsius[0]=40; send_frame(app,source,frame); assert(last(50).text=="--");

    clock_ms=3000; fresh(frame,55,75,40);
    send_frame(app,wrong_source,frame); assert(last(50).text=="--");
    char oversized[sizeof frame+1]; memcpy(oversized,&frame,sizeof frame); oversized[sizeof frame]=1;
    send_bytes(app,source,oversized,sizeof oversized); assert(last(50).text=="--");
    send_bytes(app,source,&frame,sizeof frame-1); assert(last(50).text=="--");
    send_frame(app,source,frame); assert(last(50).text=="55.0 \260C");
    frame.celsius[0]=90; send_frame(app,source,frame); assert(last(50).text=="55.0 \260C");
    clock_ms=3500; fresh(frame,90,75,40);
    frame.sample[0]=clock_ms+1; send_frame(app,source,frame); assert(last(50).text=="55.0 \260C");
    frame.sample[0]=clock_ms; frame.celsius[0]=NAN; send_frame(app,source,frame); assert(last(50).text=="--");
    clock_ms=3600; fresh(frame,INFINITY,75,40); send_frame(app,source,frame); assert(last(50).text=="--");
    clock_ms=3700; fresh(frame,181,75,40); send_frame(app,source,frame); assert(last(50).text=="--");
    clock_ms+=WARM_STALE_MS; app.tick();
    assert(last(51).text=="--" && last(52).text=="--" && last(61).number==58100);
    assert(last(56).text.find("unknown")!=std::string::npos); advisory();

    // Estimates use actual per-fluid trends; stale/missing data cannot yield a complete ETA.
    visible(false); clock_ms=10000; visible(true);
    fresh(frame,40,47,15); send_frame(app,source,frame);
    assert(last(53).text=="Measuring warm-up trend");
    clock_ms=25000; fresh(frame,50,52,20); send_frame(app,source,frame);
    assert(last(53).text=="Estimated time to app targets  1:09"); advisory();
    clock_ms=26000; fresh(frame,85,75,40); send_frame(app,source,frame);
    assert(last(53).text=="App temperature targets met");
    clock_ms=31000;
    frame.sample[1]=frame.sample[2]=clock_ms; frame.celsius[1]=73; frame.celsius[2]=37;
    send_frame(app,source,frame);
    assert(last(50).text=="--" && last(55).text.find("unknown")!=std::string::npos);
    assert(last(56).text.find("met")!=std::string::npos && last(57).text.find("met")!=std::string::npos);
    assert(last(53).text=="Waiting for all three temperatures");
    // Reopening rejects pre-open readings and resets every bar/target state.
    visible(false); clock_ms=32000; visible(true);
    send_frame(app,source,frame);
    assert(last(50).text=="--" && last(51).text=="--" && last(52).text=="--");
    assert(last(55).text.find("unknown")!=std::string::npos); advisory();
    assert(app.onFinalize());
    assert(app.onInit() && last(60).number==58100 && last(61).number==58100 && last(62).number==58100);
    assert(app.onFinalize()); close(source); close(wrong_source);
    assert(launch_attempts>0);
    puts("WarmDeck production UDP, independent targets, trends, stale/invalid/reordered data and advisory display passed.");
}
