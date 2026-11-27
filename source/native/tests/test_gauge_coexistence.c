#define _POSIX_C_SOURCE 200809L
#include "gauge_coexistence.h"
#include <assert.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>

static void port_isolation(void) {
    const unsigned data[]={42860,42863,42871,42872,42873};
    const char *names[]={"boostdeck","shiftdeck","launchdeck","powerdeck","basediagnostik","warmdeck"};
    const unsigned active[]={42971,42972,42973,42980,42982,42978};
    const unsigned most[]={42975,42976,42977,42981,42983,42979};
    int fds[19],n=0;
    unsigned i;
    for(i=0;i<sizeof data/sizeof data[0];i++) {fds[n]=gauge_bind(data[i]);assert(fds[n++]>=0);}
    for(i=0;i<sizeof names/sizeof names[0];i++) {
        assert(gauge_source_port(names[i])==active[i] && gauge_most_port(names[i])==most[i]);
        fds[n]=gauge_bind(active[i]);assert(fds[n]>=0);
        assert((fcntl(fds[n],F_GETFD)&FD_CLOEXEC) && (fcntl(fds[n],F_GETFL)&O_NONBLOCK));n++;
        assert(gauge_active(active[i]));
        fds[n]=gauge_bind(most[i]);assert(fds[n++]>=0);
    }
    fds[n]=gauge_bind(HUD_TELEMETRY_PORT);assert(fds[n++]>=0);
    fds[n]=gauge_bind(GAUGE_MOST_PORT);assert(fds[n++]>=0);
    while(n)close(fds[--n]);
    assert(!gauge_source_port(NULL) && !gauge_source_port("unknown") && !gauge_most_port("enginecodes"));
    puts("legacy IPC + every coordination port can bind together; markers exclusive/CLOEXEC");
}

static void wire_and_handoff(void) {
    int hud=gauge_bind(HUD_TELEMETRY_PORT),producer=gauge_bind(BOOST_ACTIVE_PORT);
    struct hud_telemetry sent={HUD_TELEMETRY_MAGIC,123,120,BOOST_ACTIVE_PORT,
                              HUD_SAMPLE_RPM|HUD_SAMPLE_BOOST|HUD_SAMPLE_N57,800,188},got;
    struct sockaddr_in peer;
    socklen_t size=sizeof peer;
    assert(hud>=0 && producer>=0 && sizeof sent==40);
    gauge_publish(producer,&sent);
    { struct pollfd ready={hud,POLLIN,0};assert(poll(&ready,1,1000)==1); }
    assert(recvfrom(hud,&got,sizeof got,0,(struct sockaddr *)&peer,&size)==40);
    assert(!memcmp(&sent,&got,40) && peer.sin_addr.s_addr==htonl(0x7f000001u));
    assert(ntohs(peer.sin_port)==BOOST_ACTIVE_PORT);
    close(hud);close(producer);
    { int owner=gauge_bind(GAUGE_MOST_PORT);char request=0;assert(owner>=0);
      gauge_request_most();
      { struct pollfd ready={owner,POLLIN,0};assert(poll(&ready,1,1000)==1); }
      assert(recv(owner,&request,1,0)==1 && request==1);close(owner); }
    puts("40-byte timestamped telemetry uses its source marker; HUD receives release request");
}

static void killed_owner_releases_markers(void) {
    int ready[2],hold[2],status;char byte=0;pid_t child;
    assert(!pipe(ready) && !pipe(hold));fflush(NULL);child=fork();assert(child>=0);
    if(!child) {
        int a=gauge_bind(POWER_ACTIVE_PORT),m=gauge_bind(POWER_MOST_PORT);
        close(ready[0]);close(hold[1]);byte=a>=0 && m>=0 ? 'y':'n';
        (void)write(ready[1],&byte,1);
        while(read(hold[0],&byte,1)>0) {}
        _exit(0);
    }
    close(ready[1]);close(hold[0]);assert(read(ready[0],&byte,1)==1 && byte=='y');
    assert(gauge_active(POWER_ACTIVE_PORT) && gauge_active(POWER_MOST_PORT));
    assert(!kill(child,SIGKILL) && waitpid(child,&status,0)==child && WIFSIGNALED(status));
    close(ready[0]);close(hold[1]);
    assert(!gauge_active(POWER_ACTIVE_PORT) && !gauge_active(POWER_MOST_PORT));
    puts("SIGKILL releases active/MOST ownership without deleting stale files");
}

static void projection_states(void) {
    unsetenv("ID6_FAKE_AAD_INSTALLED");unsetenv("ID6_FAKE_AAD_SHOWN");unsetenv("ID6_FAKE_CARPLAY_HUD");
    assert(!native_android_auto_installed() && !projection_hud_installed());
    assert(!setenv("ID6_FAKE_AAD_INSTALLED","1",1));
    assert(native_android_auto_installed() && projection_hud_installed());
    unsetenv("ID6_FAKE_AAD_INSTALLED");assert(!setenv("ID6_FAKE_AAD_SHOWN","1",1));
    assert(!native_android_auto_installed() && projection_hud_installed());
    unsetenv("ID6_FAKE_AAD_SHOWN");assert(!setenv("ID6_FAKE_CARPLAY_HUD","1",1));
    assert(projection_hud_installed());
    assert(!setenv("ID6_FAKE_CARPLAY_HUD","0",1) && !projection_hud_installed());
    unsetenv("ID6_FAKE_CARPLAY_HUD");
    puts("absent/AAD-installed/AAD-shown/CarPlay states are separate renderer vetoes");
}

int main(void) {
    port_isolation();wire_and_handoff();killed_owner_releases_markers();projection_states();
    puts("gauge coexistence: four behavioral cases PASS");return 0;
}
