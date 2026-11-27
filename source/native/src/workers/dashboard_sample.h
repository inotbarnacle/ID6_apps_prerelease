#ifndef ID6_HUD_DASHBOARD_SAMPLE_H
#define ID6_HUD_DASHBOARD_SAMPLE_H

#include "hud_sample.h"
#include "gauge_coexistence.h"
#include <string.h>

static inline bool hud_known_source(unsigned source) {
    return source==BOOST_ACTIVE_PORT || source==SHIFT_ACTIVE_PORT || source==LAUNCH_ACTIVE_PORT ||
           source==POWER_ACTIVE_PORT || source==BASE_ACTIVE_PORT || source==WARM_ACTIVE_PORT;
}

static inline unsigned hud_active_source() {
    const unsigned sources[]={SHIFT_ACTIVE_PORT,BOOST_ACTIVE_PORT,POWER_ACTIVE_PORT,LAUNCH_ACTIVE_PORT,BASE_ACTIVE_PORT,WARM_ACTIVE_PORT};
    for (unsigned i=0;i<sizeof sources/sizeof sources[0];++i)
        if (gauge_active(sources[i])) return sources[i];
    return 0;
}

static inline bool hud_dashboard_most_active() {
    return gauge_active(BOOST_MOST_PORT) || gauge_active(SHIFT_MOST_PORT) || gauge_active(LAUNCH_MOST_PORT) ||
           gauge_active(POWER_MOST_PORT) || gauge_active(BASE_MOST_PORT) || gauge_active(WARM_MOST_PORT);
}

class HudDashboard {
    unsigned source_;
    uint64_t opened_, rpm_seen_, boost_seen_;
    hud_sample sample_;

    static void drain(int socket) {
        char data[sizeof(hud_telemetry)+1];
        for (unsigned i=0;i<64 && recv(socket,data,sizeof data,0)>0;++i) {}
    }

public:
    HudDashboard() : source_(0),opened_(0),rpm_seen_(0),boost_seen_(0) {
        memset(&sample_,0,sizeof sample_); sample_.gear='-';
    }
    unsigned source() const { return source_; }
    void reset(int socket,uint64_t now) {
        source_=0; opened_=now; rpm_seen_=boost_seen_=0;
        memset(&sample_,0,sizeof sample_); sample_.gear='-';
        drain(socket);
    }

    bool accept(const void* data,size_t size,const sockaddr_in& peer,socklen_t peer_size,
                unsigned source,uint64_t now,bool n57) {
        hud_telemetry frame;
        if (!source || source!=source_ || !hud_known_source(source) || size!=sizeof frame ||
            peer_size<sizeof peer || peer.sin_family!=AF_INET ||
            peer.sin_addr.s_addr!=htonl(0x7f000001u) || ntohs(peer.sin_port)!=source) return false;
        memcpy(&frame,data,sizeof frame);
        if (frame.magic!=HUD_TELEMETRY_MAGIC || frame.source!=source ||
            (frame.valid&~(HUD_SAMPLE_RPM|HUD_SAMPLE_BOOST|HUD_SAMPLE_N57)) ||
            ((frame.valid&HUD_SAMPLE_N57)!=0)!=n57 || frame.rpm_ms>now || frame.boost_ms>now) return false;
        if ((frame.valid&HUD_SAMPLE_RPM) && (!frame.rpm_ms || frame.rpm>(n57 ? 6000u : 8000u) ||
            frame.rpm_ms<opened_ || frame.rpm_ms<rpm_seen_)) return false;
        if ((frame.valid&HUD_SAMPLE_BOOST) && (!frame.boost_ms ||
            frame.boost_tenths<(n57 ? -87 : -138) || frame.boost_tenths>(n57 ? 508 : 595) ||
            frame.boost_ms<opened_ || frame.boost_ms<boost_seen_)) return false;
        memset(&sample_,0,sizeof sample_); sample_.gear='-'; sample_.n57=n57;
        if (frame.valid&HUD_SAMPLE_RPM) {
            rpm_seen_=frame.rpm_ms;
            if (now-frame.rpm_ms<HUD_RPM_STALE_MS) {
                sample_.rpm=frame.rpm; sample_.rpm_ms=frame.rpm_ms;
            }
        }
        if (frame.valid&HUD_SAMPLE_BOOST) {
            boost_seen_=frame.boost_ms;
            if (now-frame.boost_ms<HUD_BOOST_STALE_MS) {
                sample_.have_boost=1; sample_.boost_ms=frame.boost_ms;
                sample_.boost_tenths=frame.boost_tenths;
            }
        }
        return true;
    }

    int poll(int socket,hud_reader& reader,const char* host,bool requirePark,uint64_t now,hud_sample& output) {
        if (reader.config_error) { hud_reader_close(&reader); reset(socket,now); return -1; }
        const unsigned active=hud_active_source();
        if (active!=source_) {
            hud_reader_close(&reader); reset(socket,now); source_=active;
            memset(&output,0,sizeof output); output.gear='-';
        }
        if (!active) return hud_reader_poll(&reader,host,&output,requirePark);
        hud_reader_close(&reader); // An active producer owns diagnostics even with no usable RPM.
        if (requirePark) { // A stream cannot establish EGS Park.
            drain(socket); memset(&sample_,0,sizeof sample_); sample_.gear='-'; output=sample_; return -1;
        }
        for (unsigned i=0;i<64;++i) {
            char data[sizeof(hud_telemetry)+1];
            sockaddr_in peer;
            socklen_t size=sizeof peer;
            const ssize_t count=recvfrom(socket,data,sizeof data,0,reinterpret_cast<sockaddr*>(&peer),&size);
            if (count<0) break;
            (void)accept(data,static_cast<size_t>(count),peer,size,active,now,reader.n57_index!=0);
        }
        output=sample_;
        return 1;
    }
};
#endif
