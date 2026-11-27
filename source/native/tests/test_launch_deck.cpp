/* Exercise the production HMI class. Only clock, spawning and process exits are fixtures. */
#include "gauge_core.h"
#include <assert.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>

static uint64_t fixture_now=100000;
static unsigned launches,signals;
static pid_t current_child;
static bool child_alive;
static uint64_t fixture_mono_ms() { return fixture_now; }
static pid_t fixture_launch(const char*const* args,int) {
    assert(!child_alive);
    assert(!strcmp(args[0],"/var/opt/hmi/data/ID5/Plugins/LaunchDeck/shift-deck-qnx"));
    assert(!strcmp(args[3],"--udp-port") && !strcmp(args[4],"42871") && !strcmp(args[5],"--speed-only"));
    ++launches;child_alive=true;return current_child=1000+launches;
}
static pid_t fixture_waitpid(pid_t child,int*,int flags) {
    assert(child==current_child && flags==WNOHANG);
    if (child_alive) return 0;
    return child;
}
static int fixture_kill(pid_t child,int signal) {
    assert(child==current_child && child_alive && signal==SIGTERM);
    ++signals;return 0; // A bounded DSI shutdown may finish after focus has already returned.
}
#define DIAGNOSTIC_WORKER_H
#define launch fixture_launch
#define waitpid fixture_waitpid
#define kill fixture_kill
#define bd_mono_ms fixture_mono_ms
#ifndef ID6_LAUNCH_SOURCE
#define ID6_LAUNCH_SOURCE "../src/plugins/launch_deck.cpp"
#endif
#include ID6_LAUNCH_SOURCE
#undef launch
#undef waitpid
#undef kill
#undef bd_mono_ms

static void tick(LaunchDeck& app) {
    fixture_now+=1100;
    app.testDeliver(app.testLastMessage(kLaunchPoll));
}

static void sample(LaunchDeck& app,int sender,uint64_t sampled,int mkph) {
    drive_n57_frame packet={DRIVE_N57_MAGIC,sampled,-1,mkph};
    struct sockaddr_in target;memset(&target,0,sizeof target);
    target.sin_family=AF_INET;target.sin_port=htons(kLaunchUdpPort);
    target.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(sendto(sender,&packet,sizeof packet,0,reinterpret_cast<struct sockaddr*>(&target),sizeof target)==
           static_cast<ssize_t>(sizeof packet));
    usleep(1000);app.testDeliver(app.testLastMessage(kLaunchPoll));
}

static void replay(LaunchDeck& app,int sender) {
    unsigned long long sampled,received;
    uint64_t previous=0;
    int rpm,speed;
    unsigned count=0;
    while (scanf("%llu %llu %d %d",&sampled,&received,&rpm,&speed)==4) {
        assert(rpm==-1 && speed==0 && received>0);
        if (previous) {
            fixture_now=received-1;app.testDeliver(app.testLastMessage(kLaunchPoll));
            const bool live=fixture_now>=previous && fixture_now-previous<750;
            assert(app.images[kModelStatus]==kLaunchStatusBase+(live ? kStatusReady : kStatusNodata));
        }
        fixture_now=received;sample(app,sender,sampled,speed);
        const bool live=received>=sampled && received-sampled<750;
        assert(app.images[kModelStatus]==kLaunchStatusBase+(live ? kStatusReady : kStatusNodata));
        previous=sampled;++count;
    }
    assert(count>0);
    if (fixture_now<previous+750) fixture_now=previous+750;
    app.testDeliver(app.testLastMessage(kLaunchPoll));
    assert(app.images[kModelStatus]==kLaunchStatusBase+kStatusNodata);
}

int main(int argc,char** argv) {
    assert(DRIVE_STALE_MS==750);
    bmw::IDispatcher dispatcher;bmw::Bundle bundle;bmw::RHMIInterfaceVersion version={1};
    LaunchDeck app(dispatcher,bmw::BasicString<char>("LaunchDeck"),1,0,version,bundle,bundle);
    assert(app.onInit());
    app.testEvent(100,true);tick(app);assert(launches==1);
    if (argc==2 && !strcmp(argv[1],"--most-replay")) {
        int sender=socket(AF_INET,SOCK_DGRAM,0);assert(sender>=0);
        replay(app,sender);close(sender);app.onFinalize();child_alive=false;
        puts("LaunchDeck actual class: delayed native MOST packet replay OK");return 0;
    }
    assert(argc==1);
    /* MENU can leave this page visible as a preview. Its focus map must stop
     * the actual child; a later poll after child exit must not launch another. */
    app.testState(100,1u,4u,0);
    assert(signals==1);
    child_alive=false;tick(app);tick(app);assert(launches==1);
    app.testState(100,11u,23u,1);tick(app);assert(launches==1);

    /* Return in either event order; visibility without focus never starts diagnostics. */
    app.testState(100,11u,23u,0);
    app.testState(100,1u,4u,1);tick(app);assert(launches==1);
    app.testState(100,11u,23u,1);tick(app);assert(launches==2);
    app.testState(100,11u,23u,1);tick(app);assert(launches==2);

    /* Rapid exit/return does not spawn a second worker while the first stops. */
    app.testState(100,1u,4u,0);assert(signals==2);
    app.testState(100,1u,4u,1);tick(app);assert(launches==2);
    child_alive=false;tick(app);assert(launches==3);

    /* Missing, wrong-key and invalid bool activity are fail-closed. */
    bmw::Map<unsigned,bmw::Variant> empty;
    app.testMap(100,1u,empty);assert(signals==3);
    child_alive=false;tick(app);
    app.testState(100,1u,23u,1);tick(app);assert(launches==3);
    app.testState(100,1u,4u,2);tick(app);assert(launches==3);
    app.testState(100,1u,4u,1);tick(app);assert(launches==4);
    app.testMap(100,11u,empty);assert(signals==4);
    child_alive=false;tick(app);tick(app);assert(launches==4);

    /* A queued callback from a finalized instance cannot restart a child. */
    unsigned queued=app.testLastMessage(kLaunchPoll);
    app.onFinalize();app.testDeliver(queued);assert(launches==4);
    assert(app.onInit());app.testDeliver(queued);assert(launches==4);
    app.testState(100,11u,23u,1);tick(app);assert(launches==4);
    app.testState(100,1u,4u,1);tick(app);assert(launches==5);

    int sender=socket(AF_INET,SOCK_DGRAM,0);assert(sender>=0);
    const uint64_t sequence=fixture_now+100;
    for (unsigned i=0;i<4;++i) {
        const uint64_t sampled=sequence+i*325;
        fixture_now=sampled+80;sample(app,sender,sampled,0);
        assert(app.images[kModelStatus]==kLaunchStatusBase+kStatusReady);
        fixture_now=sampled+400;app.testDeliver(app.testLastMessage(kLaunchPoll));
        assert(app.images[kModelStatus]==kLaunchStatusBase+kStatusReady);
    }
    const uint64_t final_sample=sequence+3*325;
    fixture_now=final_sample+749;app.testDeliver(app.testLastMessage(kLaunchPoll));
    assert(app.images[kModelStatus]==kLaunchStatusBase+kStatusReady);
    sample(app,sender,fixture_now+5000,0); /* A future datagram cannot extend the previous sample. */
    fixture_now=final_sample+750;app.testDeliver(app.testLastMessage(kLaunchPoll));
    assert(app.images[kModelStatus]==kLaunchStatusBase+kStatusNodata);
    fixture_now+=100;sample(app,sender,fixture_now-800,0);
    assert(app.images[kModelStatus]==kLaunchStatusBase+kStatusNodata);
    fixture_now+=100;sample(app,sender,fixture_now,0);
    assert(app.images[kModelStatus]==kLaunchStatusBase+kStatusReady);
    fixture_now+=50;sample(app,sender,fixture_now,-1);
    assert(app.images[kModelStatus]==kLaunchStatusBase+kStatusNodata);
    close(sender);
    app.onFinalize();assert(signals==5);child_alive=false;
    puts("LaunchDeck actual class: map focus/MENU stop, no hidden respawn, preview, event order, rapid return and stale callbacks OK");
}
