#define _POSIX_C_SOURCE 200809L
#include "dashboard_sample.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>

static unsigned polls, closes;
extern "C" void hud_reader_close(hud_reader* reader) {
    ++closes; reader->telemetry_initialized=0; reader->fd=reader->lock=-1;
}
extern "C" int hud_reader_poll(hud_reader*,const char*,hud_sample* sample,int requirePark) {
    ++polls; memset(sample,0,sizeof *sample); sample->rpm=800;
    sample->rpm_ms=20000; sample->gear=requirePark ? 'P' : '-'; return 1;
}

static void clean_env() {
    unsetenv("ID6_FAKE_AAD_INSTALLED"); unsetenv("ID6_FAKE_CARPLAY_HUD"); unsetenv("ID6_FAKE_AAD_SHOWN");
}

static void publish(int listener,int producer,const hud_telemetry& packet) {
    gauge_publish(producer,&packet);
    pollfd ready={listener,POLLIN,0};
    assert(::poll(&ready,1,500)==1 && (ready.revents&POLLIN));
}

int main() {
    clean_env();
    assert(!projection_hud_installed());
    assert(hud_known_source(WARM_ACTIVE_PORT));
    const int warm_most=gauge_bind(WARM_MOST_PORT); assert(warm_most>=0);
    assert(hud_dashboard_most_active());
    close(warm_most);
    setenv("ID6_FAKE_AAD_INSTALLED","1",1); assert(projection_hud_installed()); clean_env();
    setenv("ID6_FAKE_CARPLAY_HUD","1",1); assert(projection_hud_installed()); clean_env();
    setenv("ID6_FAKE_AAD_SHOWN","1",1); assert(projection_hud_installed()); clean_env();
    const int socket=gauge_bind(HUD_TELEMETRY_PORT); assert(socket>=0);
    int producer=gauge_bind(BOOST_ACTIVE_PORT); assert(producer>=0);
    hud_reader reader; memset(&reader,0,sizeof reader); reader.fd=reader.lock=-1; reader.n57_index=0x010203;
    HudDashboard dashboard; hud_sample output;
    assert(dashboard.poll(socket,reader,"unused",false,10000,output)==1);
    assert(!polls && dashboard.source()==BOOST_ACTIVE_PORT && !output.rpm_ms);
    hud_telemetry packet={HUD_TELEMETRY_MAGIC,10000,10000,BOOST_ACTIVE_PORT,
                         HUD_SAMPLE_RPM|HUD_SAMPLE_BOOST|HUD_SAMPLE_N57,800,189};
    sockaddr_in peer=gauge_local_address(BOOST_ACTIVE_PORT);
    assert(dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    assert(dashboard.poll(socket,reader,"unused",false,10000,output)==1 && output.rpm==800 &&
           output.rpm_ms==10000 && output.n57 && output.have_boost && output.boost_ms==10000);
    hud_telemetry invalid=packet;
    invalid.magic^=1; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.valid|=8; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.source=POWER_ACTIVE_PORT; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.rpm=6001; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.boost_tenths=509; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.boost_tenths=-88; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.rpm_ms=10001; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.boost_ms=10001; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.rpm_ms=9999; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.boost_ms=9999; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    invalid=packet; invalid.rpm_ms=0; assert(!dashboard.accept(&invalid,sizeof invalid,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    assert(!dashboard.accept(&packet,sizeof packet-1,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    assert(!dashboard.accept(&packet,sizeof packet+1,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer-1,BOOST_ACTIVE_PORT,10000,true));
    assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,false));
    peer.sin_port=htons(POWER_ACTIVE_PORT); assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    peer=gauge_local_address(BOOST_ACTIVE_PORT); peer.sin_addr.s_addr=htonl(0x7f000002u);
    assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));
    peer=gauge_local_address(BOOST_ACTIVE_PORT); peer.sin_family=AF_UNSPEC;
    assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,10000,true));

    peer=gauge_local_address(BOOST_ACTIVE_PORT);
    assert(dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,11100,true));
    assert(dashboard.poll(socket,reader,"unused",false,11100,output)==1 && !output.rpm_ms && output.have_boost);
    packet.rpm_ms=11200;
    assert(dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,11200,true));
    assert(dashboard.poll(socket,reader,"unused",false,11200,output)==1 && output.rpm_ms==11200 && !output.have_boost);
    packet.rpm_ms=10000; assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,11200,true));
    packet.rpm_ms=11200; packet.valid=HUD_SAMPLE_N57;
    assert(dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,11200,true));
    assert(dashboard.poll(socket,reader,"unused",false,11200,output)==1 && !output.rpm_ms && !output.have_boost);

    packet.rpm_ms=11300; packet.boost_ms=11300; packet.valid=HUD_SAMPLE_RPM|HUD_SAMPLE_BOOST|HUD_SAMPLE_N57;
    publish(socket,producer,packet);
    assert(dashboard.poll(socket,reader,"unused",false,11300,output)==1 && output.rpm_ms==11300 && output.have_boost);
    assert(dashboard.poll(socket,reader,"unused",true,11300,output)==-1 && !polls && !output.rpm_ms); // Never infer Park from UDP.
    close(producer);
    producer=gauge_bind(POWER_ACTIVE_PORT); assert(producer>=0);
    packet.source=BOOST_ACTIVE_PORT; packet.rpm_ms=11400; publish(socket,producer,packet);
    assert(dashboard.poll(socket,reader,"unused",false,11400,output)==1 && !output.rpm_ms && !polls);
    packet.source=POWER_ACTIVE_PORT; packet.rpm_ms=11399; packet.boost_ms=11399; publish(socket,producer,packet);
    assert(dashboard.poll(socket,reader,"unused",false,11400,output)==1 && !output.rpm_ms && !polls);
    packet.rpm_ms=11401; packet.boost_ms=0; packet.valid=HUD_SAMPLE_RPM|HUD_SAMPLE_N57;
    publish(socket,producer,packet);
    assert(dashboard.poll(socket,reader,"unused",false,11401,output)==1 && output.rpm_ms==11401 && !output.have_boost);
    close(producer);
    producer=gauge_bind(BASE_ACTIVE_PORT); assert(producer>=0);
    assert(dashboard.poll(socket,reader,"unused",false,11500,output)==1 && !output.rpm_ms && !polls);
    close(producer);
    const unsigned empty_sources[]={SHIFT_ACTIVE_PORT,LAUNCH_ACTIVE_PORT,WARM_ACTIVE_PORT};
    for(unsigned i=0;i<sizeof empty_sources/sizeof empty_sources[0];++i) {
        producer=gauge_bind(empty_sources[i]);assert(producer>=0);
        assert(dashboard.poll(socket,reader,"unused",false,11600+i,output)==1 && !output.rpm_ms && !polls);
        close(producer);
    }
    assert(dashboard.poll(socket,reader,"unused",false,20000,output)==1 && polls==1 && !dashboard.source());
    reader.config_error=1;
    assert(dashboard.poll(socket,reader,"unused",false,20000,output)==-1 && polls==1);
    reader.config_error=0; reader.n57_index=0;
    producer=gauge_bind(BOOST_ACTIVE_PORT);assert(producer>=0);
    assert(dashboard.poll(socket,reader,"unused",false,21000,output)==1 && polls==1);
    packet.source=BOOST_ACTIVE_PORT;packet.rpm_ms=packet.boost_ms=21000;
    packet.rpm=8000;packet.boost_tenths=595;packet.valid=HUD_SAMPLE_RPM|HUD_SAMPLE_BOOST;
    peer=gauge_local_address(BOOST_ACTIVE_PORT);
    assert(dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,21000,false));
    assert(dashboard.poll(socket,reader,"unused",false,21000,output)==1 && output.rpm==8000 && !output.n57);
    packet.rpm=8001;assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,21000,false));
    packet.rpm=8000;packet.boost_tenths=-100; // Existing gasoline decoder permits idle vacuum.
    assert(dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,21000,false));
    assert(dashboard.poll(socket,reader,"unused",false,21000,output)==1 && output.have_boost && output.boost_tenths==-100);
    packet.boost_tenths=-139;
    assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,21000,false));
    packet.boost_tenths=596;
    assert(!dashboard.accept(&packet,sizeof packet,peer,sizeof peer,BOOST_ACTIVE_PORT,21000,false));
    close(producer);
    assert(closes>0); close(socket);
    puts("HUD dashboard: strict per-channel IPC, source handoff, empty producer suppresses second poller and projection veto OK");
}
