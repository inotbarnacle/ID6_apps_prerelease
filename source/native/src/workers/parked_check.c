#define _POSIX_C_SOURCE 200809L
#include "gauge_core.h"
#include "diagnostic_lease.h"
#include "diag_link.h"
#include "n57.h"
#include <math.h>

#include "hud_sample.h"

static int native_exchange(void *ctx, unsigned ecu, const unsigned char *req, unsigned n,
                            const unsigned char *prefix, unsigned pn, unsigned char *reply,
                            unsigned cap, unsigned timeout, unsigned *negative) {
    return diag_hsfz_exchange(*(int *)ctx,ecu,req,n,prefix,pn,reply,cap,timeout,negative);
}

static int telemetry_exchange(void *ctx, unsigned ecu, const unsigned char *req, unsigned n,
                              const unsigned char *prefix, unsigned pn, unsigned char *reply,
                              unsigned cap, unsigned timeout, unsigned *negative) {
    return diag_link_request(ctx,ecu,req,n,prefix,pn,reply,cap,timeout,negative);
}

static int pressure(int fd, unsigned did, double *kpa) {
    const unsigned char request[] = {0x22, did >> 8, did & 0xff};
    const unsigned char prefix[] = {0x62, request[1], request[2]};
    unsigned char reply[5];
    unsigned negative;
    int count = diag_hsfz_exchange(fd, 0x12, request, 3, prefix, 3, reply, sizeof reply, 150, &negative);
    if (count == -1) return -1;
    return count == 5 && bd_decode_did(did, reply + 3, 2, kpa);
}

void hud_reader_init(hud_reader *reader) {
    sigset_t alarms;
    memset(reader, 0, sizeof *reader);
    reader->fd = reader->lock = -1;
    reader->config_error=n57_settings(&reader->n57_index)<0;
    /* The HUD owns SIGTERM; retain its handler while enabling the DSI watchdog. */
    signal(SIGALRM,SIG_DFL);
    sigemptyset(&alarms); sigaddset(&alarms,SIGALRM);
    sigprocmask(SIG_UNBLOCK,&alarms,0);
}

void hud_reader_close(hud_reader *reader) {
    release_gateway(&reader->fd, &reader->lock);
    if (reader->telemetry_initialized) diag_link_close(&reader->telemetry);
    reader->telemetry_initialized=0;
    reader->lease_until=reader->retry_at=reader->boost_due=reader->boost_seen=0;
}

static int n57_telemetry_poll(hud_reader *reader, const char *host, hud_sample *sample) {
    struct diag_link *link=&reader->telemetry;
    double rpm=0;
    uint64_t rpm_started;
    int rc, complete=1;
    if (!reader->telemetry_initialized) {
        struct diag_link_cfg cfg;
        if (strlen(host)>=sizeof reader->telemetry_host) return -1;
        strcpy(reader->telemetry_host,host);
        memset(&cfg,0,sizeof cfg);
        /* Rendering already owns a MOST client. Reuse dashboard data when that
         * bus is needed; never open a second diagnostic DSI client here. */
        cfg.host=reader->telemetry_host; cfg.port=6801; cfg.transport=DIAG_HSFZ;
        cfg.kind=DIAG_LOOP; cfg.dsi_name="hudgauges"; cfg.session=1;
        diag_link_init(link,&cfg);
        reader->telemetry_initialized=1;
    }
    diag_link_tick(link);
    if (diag_link_lost(link)) { reader->boost_seen=0; return -1; }
    if (!diag_link_up(link) || !diag_link_begin(link)) return 0;
    rc=n57_match(telemetry_exchange,link,reader->n57_index);
    rpm_started=bd_mono_ms();
    if (rc==1) rc=n57_read(telemetry_exchange,link,0x5955,&rpm);
    if (rc!=1) {
        diag_link_end(link,0);
        if (rc<0) diag_link_drop(link);
        reader->boost_seen=0;
        return -1;
    }
    sample->rpm=(unsigned)round(rpm);
    sample->rpm_ms=rpm_started;
    sample->gear='-'; sample->n57=1;
    if (rpm_started>=reader->boost_due) {
        double absolute=0, ambient=0;
        const uint64_t boost_started=bd_mono_ms();
        rc=n57_read(telemetry_exchange,link,0x4841,&absolute);
        if (rc==1) rc=n57_read(telemetry_exchange,link,0x4cf0,&ambient);
        reader->boost_due=bd_mono_ms()+450;
        if (rc==1) {
            reader->boost_tenths=(int)round((absolute-ambient)*.145037738);
            reader->boost_seen=boost_started;
        } else {
            reader->boost_seen=0;
            complete=0;
        }
    }
    diag_link_end(link,complete);
    if (rc<0) diag_link_drop(link);
    (void)diag_link_should_yield(link,bd_mono_ms());
    sample->boost_ms=reader->boost_seen;
    sample->have_boost=reader->boost_seen && bd_mono_ms()-reader->boost_seen<HUD_BOOST_STALE_MS;
    sample->boost_tenths=reader->boost_tenths;
    return 1;
}

static void pause_gateway(hud_reader *reader, unsigned wait_ms) {
    release_gateway(&reader->fd, &reader->lock);
    reader->retry_at = bd_mono_ms() + wait_ms;
}

int hud_reader_poll(hud_reader *reader, const char *host, hud_sample *sample, int require_park) {
    const unsigned char rpm[] = {1, 0x0c}, rpm_prefix[] = {0x41, 0x0c};
    const unsigned char speed[] = {1, 0x0d}, speed_prefix[] = {0x41, 0x0d};
    const unsigned char gear[] = {0x22, 0xda, 0x2e}, gear_prefix[] = {0x62, 0xda, 0x2e};
    unsigned char reply[5];
    unsigned negative;
    uint64_t now = bd_mono_ms();
    if (reader->config_error) return -1;
    if (reader->n57_index && !require_park) {
        if (reader->fd>=0 || reader->lock>=0) {
            release_gateway(&reader->fd,&reader->lock);
            reader->lease_until=reader->retry_at=reader->boost_due=reader->boost_seen=0;
        }
        return n57_telemetry_poll(reader,host,sample);
    }
    if (reader->telemetry_initialized) {
        diag_link_close(&reader->telemetry);
        reader->telemetry_initialized=0;
        reader->boost_due=reader->boost_seen=0;
    }
    if (reader->fd < 0) {
        if (now < reader->retry_at) return 0;
        reader->lock = reserve_gateway();
        if (reader->lock < 0) return 0;
        reader->fd = diag_hsfz_connect(host, 6801);
        if (reader->fd < 0) {
            pause_gateway(reader, DIAGNOSTIC_RETRY_MS);
            return 0;
        }
        reader->lease_until = bd_mono_ms() + DIAGNOSTIC_LEASE_MS;
    }
    int fd = reader->fd;
    int permitted=1, have_rpm=0;
    double rpm_value=0, kph=0;
    if (reader->n57_index) permitted=n57_match(native_exchange,&fd,reader->n57_index)==1;
    if (permitted && require_park) {
        permitted=reader->n57_index ?
            n57_read(native_exchange,&fd,0x4c14,&kph)==1 && kph==0 :
            diag_hsfz_exchange(fd,0x12,speed,2,speed_prefix,2,reply,sizeof reply,350,&negative)==3 && reply[2]==0;
        if (permitted && reader->n57_index) permitted=n57_park(native_exchange,&fd)==1;
        else if (permitted) permitted=
            diag_hsfz_exchange(fd,0x18,gear,3,gear_prefix,3,reply,sizeof reply,350,&negative)==5 &&
            reply[3]==0 && reply[4]==6;
    }
    const uint64_t rpm_started=bd_mono_ms();
    if (permitted) {
        if (reader->n57_index) have_rpm=n57_read(native_exchange,&fd,0x5955,&rpm_value)==1;
        else if (diag_hsfz_exchange(fd,0x12,rpm,2,rpm_prefix,2,reply,sizeof reply,180,&negative)==4) {
            rpm_value=((unsigned)reply[2]*256+reply[3])/4;
            have_rpm=1;
        }
    }
    if (!permitted || !have_rpm) {
        reader->boost_seen=0;
        pause_gateway(reader, DIAGNOSTIC_RETRY_MS);
        return require_park || reader->n57_index ? -1 : 0;
    }
    sample->rpm = (unsigned)round(rpm_value);
    sample->rpm_ms = reader->n57_index ? rpm_started : bd_mono_ms();
    sample->gear = require_park ? 'P' : '-';
    sample->n57 = reader->n57_index!=0;

    if (sample->rpm_ms >= reader->boost_due) {
        double absolute, ambient;
        const uint64_t boost_started=bd_mono_ms();
        int result = reader->n57_index ? n57_read(native_exchange,&fd,0x4841,&absolute) : pressure(fd,0x4205,&absolute);
        if (result == 1) result = reader->n57_index ? n57_read(native_exchange,&fd,0x4cf0,&ambient) : pressure(fd,0x4201,&ambient);
        reader->boost_due = bd_mono_ms() + 450;
        if (result == 1) {
            reader->boost_tenths = (int)round((absolute - ambient) * (reader->n57_index ? .145037738 : 1.45037738));
            reader->boost_seen = reader->n57_index ? boost_started : bd_mono_ms();
        } else {
            reader->boost_seen = 0;
            if (result < 0) pause_gateway(reader, DIAGNOSTIC_YIELD_MS);
        }
    }
    sample->boost_ms=reader->boost_seen;
    sample->have_boost = reader->boost_seen && bd_mono_ms() - reader->boost_seen < HUD_BOOST_STALE_MS;
    sample->boost_tenths = reader->boost_tenths;
    if (reader->fd >= 0 && gateway_should_yield(reader->lock, bd_mono_ms(), reader->lease_until))
        pause_gateway(reader, DIAGNOSTIC_YIELD_MS);
    return 1;
}

int hud_read(const char *host, hud_sample *sample, int require_park) {
    hud_reader reader;
    hud_reader_init(&reader);
    int valid = hud_reader_poll(&reader, host, sample, require_park);
    hud_reader_close(&reader);
    return valid == 1;
}
