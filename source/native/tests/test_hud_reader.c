#define _POSIX_C_SOURCE 200809L
#include "hud_sample.h"
#include "diag_link.h"
#include "gauge_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int native, bad_index, bad_gear, bad_egs, bad_display, bad_button, bad_rpm, no_baro, clears, definitions, reads, calls;
static unsigned did, speed_raw;
static int raw_down, raw_connects, telemetry_up=1, transaction, begins, ends, last_complete;
static int no_begin, link_lost, telemetry_fail, telemetry_delay, telemetry_requests, drops, inits, transaction_requests;
static hud_reader *active_reader;
volatile sig_atomic_t diag_running=1;

void diag_link_init(struct diag_link *link,const struct diag_link_cfg *cfg) {
    assert(cfg->transport==DIAG_HSFZ && cfg->kind==DIAG_LOOP && cfg->session==1 && cfg->most_after_hsfz==0);
    assert(!strcmp(cfg->host,"model") && cfg->port==6801 && !strcmp(cfg->dsi_name,"hudgauges"));
    memset(link,0,sizeof *link); link->cfg=*cfg; link->fd=link->lock=-1; inits++;
}
int diag_link_up(struct diag_link *link) { (void)link; return telemetry_up; }
int diag_link_begin(struct diag_link *link) {
    (void)link; assert(!transaction);
    assert(active_reader && active_reader->fd==-1 && active_reader->lock==-1);
    if (no_begin) return 0;
    transaction=1; transaction_requests=0; begins++; return 1;
}
void diag_link_end(struct diag_link *link,int complete) {
    (void)link; assert(transaction); transaction=0; ends++; last_complete=complete;
}
int diag_link_lost(const struct diag_link *link) { (void)link; return link_lost; }
void diag_link_tick(struct diag_link *link) { (void)link; }
void diag_link_drop(struct diag_link *link) { (void)link; drops++; }
int diag_link_should_yield(struct diag_link *link,uint64_t now) { (void)link;(void)now;assert(!transaction);return 0; }
void diag_link_close(struct diag_link *link) { (void)link;assert(!transaction); }

int diag_hsfz_connect(const char *host, unsigned port) {
    assert(!strcmp(host,"model") && port==6801);
    assert(active_reader && !active_reader->telemetry_initialized);
    raw_connects++;
    if (raw_down) return -1;
    return socket(AF_INET,SOCK_STREAM,0);
}

int diag_link_request(struct diag_link *link,unsigned ecu,const unsigned char *q,unsigned n,
                      const unsigned char *prefix,unsigned pn,unsigned char *r,unsigned cap,
                      unsigned timeout,unsigned *negative) {
    int result;
    (void)link;
    assert(native && transaction && ecu==0x12 && q[0]!=1);
    if (!transaction_requests++) assert(n==3 && !memcmp(q,"\x22\xf1\x50",3));
    telemetry_requests++;
    if (telemetry_fail && n==8 && q[0]==0x2c) return -1;
    result=diag_hsfz_exchange(-1,ecu,q,n,prefix,pn,r,cap,timeout,negative);
    if (telemetry_delay && n==3 && q[0]==0x22 && q[1]==0xf3 && did==0x5955) {
        struct timespec pause={1,200000000}; nanosleep(&pause,0);
    }
    return result;
}

int diag_hsfz_exchange(int fd, unsigned ecu, const unsigned char *q, unsigned n,
                       const unsigned char *prefix, unsigned pn, unsigned char *r,
                       unsigned cap, unsigned timeout, unsigned *negative) {
    (void)fd; (void)cap; (void)timeout;
    calls++;
    assert(ecu==0x12 || ecu==0x18);
    memcpy(r,prefix,pn);
    if (n==3 && !memcmp(q,"\x22\xda\x2e",3)) {
        assert(ecu==0x18 && !native); r[3]=0; r[4]=bad_gear ? 5 : 6; return 5;
    }
    if (native) {
        assert(q[0]!=1); /* A configured N57 must never fall back to mode 01. */
        if (ecu==0x18) {
            if (n==3 && !memcmp(q,"\x22\xf1\x50",3)) {
                memcpy(r+3,bad_egs ? "\x04\x05\x06" : "\x0f\x14\xb0",3); return 6;
            }
            if (n==3 && !memcmp(q,"\x22\x43\x06",3)) { r[3]=bad_gear ? 5 : 8; return 4; }
            assert(n==3 && !memcmp(q,"\x22\xda\x27",3));
            memcpy(r+3,"\x00\x01\x00\x00\x00\x00",6);
            if (bad_display) r[4]=3;
            if (bad_button) r[8]=2;
            return 9;
        }
        if (n==3 && !memcmp(q,"\x22\xf1\x50",3)) {
            memcpy(r+3,bad_index ? "\x04\x05\x06" : "\x01\x02\x03",3); return 6;
        }
        if (n==4 && !memcmp(q,"\x2c\x03\xf3\x03",4)) { clears++; did=0; return 4; }
        if (n==8 && !memcmp(q,"\x2c\x01\xf3\x03",4)) {
            assert(!did && q[6]==1 && q[7]==2);
            definitions++; did=((unsigned)q[4]<<8)|q[5]; return 4;
        }
        if (n==3 && !memcmp(q,"\x22\xf3\x03",3)) {
            unsigned raw;
            reads++;
            if (did==0x5955) raw=bad_rpm ? 65535 : 1600;
            else if (did==0x4c14) raw=speed_raw;
            else if (did==0x4841) raw=25122;
            else { assert(did==0x4cf0); raw=32768; if (no_baro) { *negative=0x31; return -2; } }
            r[3]=(unsigned char)(raw>>8); r[4]=(unsigned char)raw; return 5;
        }
    } else {
        if (n==2 && q[0]==1 && q[1]==0x0c) { r[2]=0x0c; r[3]=0x80; return 4; }
        if (n==2 && q[0]==1 && q[1]==0x0d) { r[2]=(unsigned char)speed_raw; return 3; }
        if (n==3 && q[0]==0x22 && q[1]==0x42) {
            unsigned raw=q[2]==5 ? 32000 : 25600;
            r[3]=(unsigned char)(raw>>8); r[4]=(unsigned char)raw; return 5;
        }
    }
    assert(!"Unexpected HUD diagnostic request");
    return -1;
}

static int poll_case(hud_sample *sample, int park) {
    hud_reader r;
    hud_reader_init(&r);
    active_reader=&r;
    r.n57_index=native ? 0x010203 : 0; r.config_error=0;
    int result=hud_reader_poll(&r,"model",sample,park);
    hud_reader_close(&r);
    active_reader=0;
    assert(r.fd==-1 && r.lock==-1);
    return result;
}

int main(void) {
    hud_sample sample;
    { sigset_t blocked, current; struct sigaction action; hud_reader r;
      sigemptyset(&blocked); sigaddset(&blocked,SIGALRM); sigprocmask(SIG_BLOCK,&blocked,0);
      signal(SIGALRM,SIG_IGN); hud_reader_init(&r);
      assert(!sigaction(SIGALRM,0,&action) && action.sa_handler==SIG_DFL);
      assert(!sigprocmask(SIG_BLOCK,0,&current) && !sigismember(&current,SIGALRM));
      hud_reader_close(&r); }
    native=1;
    assert(poll_case(&sample,1)==1 && sample.rpm==800 && sample.gear=='P' && sample.n57);
    assert(sample.have_boost && sample.boost_tenths==189 && sample.rpm_ms<=bd_mono_ms());
    assert(hud_boost_fresh(&sample,sample.boost_ms+1199) && !hud_boost_fresh(&sample,sample.boost_ms+1200));
    assert(clears==4 && definitions==4 && reads==4);
    speed_raw=1; assert(poll_case(&sample,1)==-1); speed_raw=0;
    bad_gear=1; assert(poll_case(&sample,1)==-1); bad_gear=0;
    bad_egs=1; assert(poll_case(&sample,1)==-1); bad_egs=0;
    bad_display=1; assert(poll_case(&sample,1)==-1); bad_display=0;
    bad_button=1; assert(poll_case(&sample,1)==-1); bad_button=0;
    bad_index=1; assert(poll_case(&sample,0)==-1); bad_index=0;
    bad_rpm=1; assert(poll_case(&sample,0)==-1); bad_rpm=0;
    no_baro=1; assert(poll_case(&sample,0)==1 && !sample.have_boost && !last_complete); no_baro=0;
    { int previous=raw_connects, before=telemetry_requests;
      assert(poll_case(&sample,0)==1 && sample.rpm==800 && sample.have_boost && sample.n57 && sample.gear=='-');
      assert(raw_connects==previous && telemetry_requests==before+10 && last_complete);
      assert(begins==ends && !transaction); }
    { int before=telemetry_requests;
      telemetry_up=0; assert(poll_case(&sample,0)==0 && telemetry_requests==before); telemetry_up=1;
      no_begin=1; assert(poll_case(&sample,0)==0 && telemetry_requests==before); no_begin=0;
      link_lost=1; assert(poll_case(&sample,0)==-1 && telemetry_requests==before); link_lost=0; }
    { int before=drops;
      telemetry_fail=1; assert(poll_case(&sample,0)==-1 && !last_complete && drops==before+1);
      telemetry_fail=0; assert(begins==ends && !transaction); }
    telemetry_delay=1;
    assert(poll_case(&sample,0)==1 && bd_mono_ms()-sample.rpm_ms>=HUD_RPM_STALE_MS);
    telemetry_delay=0; /* A delayed reply is not relabelled as a fresh sample. */
    { int before=inits;
      raw_down=1; assert(poll_case(&sample,1)==0 && inits==before); raw_down=0; }
    { hud_reader r; hud_reader_init(&r); r.n57_index=0x010203; r.config_error=0; active_reader=&r;
      assert(hud_reader_poll(&r,"model",&sample,1)==1 && r.fd>=0 && r.lock>=0);
      assert(hud_reader_poll(&r,"model",&sample,0)==1 && r.fd==-1 && r.lock==-1 && r.telemetry_initialized);
      assert(hud_reader_poll(&r,"model",&sample,1)==1 && r.fd>=0 && r.lock>=0 && !r.telemetry_initialized);
      hud_reader_close(&r); active_reader=0; }
    native=0;
    assert(poll_case(&sample,1)==1 && sample.rpm==800 && !sample.n57 && sample.boost_tenths==218);
    speed_raw=1; assert(poll_case(&sample,1)==-1);
    { hud_reader r; int before=calls; hud_reader_init(&r); r.config_error=1;
      assert(hud_reader_poll(&r,"model",&sample,0)==-1 && calls==before); hud_reader_close(&r); }
    puts("HUD reader: atomic N57 telemetry/freshness, HSFZ-only Park, identity/failures and gasoline regression OK");
    return 0;
}
