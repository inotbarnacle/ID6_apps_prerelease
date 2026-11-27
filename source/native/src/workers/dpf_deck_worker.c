#define _POSIX_C_SOURCE 200809L
#include "diag_link.h"
#include "dpf_core.h"
#include "gauge_coexistence.h"
#include "gauge_core.h"
#include "obd_capabilities.h"
#include "dpf_regen_capabilities.h"
#include <limits.h>
#include <poll.h>
#include <string.h>

static int number(const char *text,uint64_t maximum,uint64_t *out) {
    uint64_t value=0;
    if (!*text) return 0;
    do {
        unsigned digit;
        if (*text<'0' || *text>'9') return 0;
        digit=(unsigned)(*text-'0');
        if (value>maximum/10 || (value==maximum/10 && digit>maximum%10)) return 0;
        value=value*10+digit;
    } while (*++text);
    if (!value) return 0;
    *out=value;
    return 1;
}

int main(int argc,char **argv) {
    struct diag_link_cfg cfg={0,6801,DIAG_AUTO,DIAG_LOOP,"dpfdeck",1};
    struct diag_link link;
    struct obd_capabilities caps={{0},{0},0};
    struct dpf_regen_capabilities regen_caps={0,0,0};
    struct dpf_frame frame;
    static const unsigned pids[]={0x05,0x7a,0x7b,0x7c,0x8b};
    uint64_t token=0,due[5]={0},observed[DPF_COUNT]={0};
    unsigned cycles=0,count=0,next=0,udp_port=DPF_PORT;
    const pid_t parent=getppid();
    int i,sock,regen_enabled=0;
    struct sockaddr_in peer;
    memset(&frame,0,sizeof frame);
    for (i=1;i<argc;++i) {
        uint64_t value;
        if (!strcmp(argv[i],"--host") && i+1<argc && !cfg.host) cfg.host=argv[++i];
        else if (!strcmp(argv[i],"--regen-status") && !regen_enabled) regen_enabled=1;
        else if ((!strcmp(argv[i],"--token") || !strcmp(argv[i],"--port") ||
                  !strcmp(argv[i],"--udp-port") || !strcmp(argv[i],"--cycles")) && i+1<argc) {
            const int token_arg=!strcmp(argv[i],"--token"),port_arg=!strcmp(argv[i],"--port"),
                      udp_arg=!strcmp(argv[i],"--udp-port");
            if (!number(argv[++i],token_arg ? UINT64_MAX : port_arg || udp_arg ? 65535 : 100000,&value)) return 2;
            if (token_arg) { if (token) return 2;token=value; }
            else if (port_arg) cfg.port=(unsigned)value;
            else if (udp_arg) udp_port=(unsigned)value;
            else cycles=(unsigned)value;
        } else if (!strcmp(argv[i],"--transport") && i+1<argc) {
            ++i;
            if (!strcmp(argv[i],"hsfz")) cfg.transport=DIAG_HSFZ;
            else if (!strcmp(argv[i],"most")) cfg.transport=DIAG_MOST;
            else if (!strcmp(argv[i],"auto")) cfg.transport=DIAG_AUTO;
            else return 2;
        } else return 2;
    }
    if (!token || parent<=1) return 2;
    /* Match the existing projection ownership policy conservatively. */
    if (projection_hud_installed()) cfg.transport=DIAG_HSFZ;
    if (cfg.transport==DIAG_HSFZ && !cfg.host) return 2;
    sock=gauge_bind(DPF_ACTIVE_PORT);
    if (sock<0) return 1;
    if (fcntl(sock,F_SETFD,FD_CLOEXEC)<0) { close(sock);return 1; }
    peer=gauge_local_address(udp_port);
    frame.token=token;frame.worker_pid=(uint32_t)getpid();
    diag_link_signals();
    diag_link_init(&link,&cfg);
    while (diag_running && getppid()==parent && (!cycles || count<cycles)) {
        if (diag_link_up(&link)) {
            const unsigned group=next,pid=pids[group],mask=dpf_pid_mask(pid),
                           before=dpf_capability_generation(&caps,&regen_caps);
            next=(next+1)%(regen_enabled?5:4);
            diag_link_tick(&link);
            if (bd_mono_ms()>=due[group]) {
                const int supported=pid==0x8b ? dpf_regen_advertised(&caps,&regen_caps,&link) :
                                               obd_pid_advertised(&caps,&link,pid);
                if (supported==0) {
                    frame.known|=mask;frame.supported&=~mask;frame.valid&=~mask;
                    due[group]=bd_mono_ms()+DPF_RETRY_MS;
                } else if (supported==1 && before==dpf_capability_generation(&caps,&regen_caps) && diag_running) {
                    const unsigned char request[]={1,(unsigned char)pid},prefix[]={0x41,(unsigned char)pid};
                    unsigned char reply[16];
                    unsigned negative;
                    struct dpf_sample sample;
                    const uint64_t started=bd_mono_ms();
                    const int n=diag_link_request(&link,0x12,request,2,prefix,2,reply,sizeof reply,450,&negative);
                    frame.valid&=~mask;
                    due[group]=bd_mono_ms()+DPF_RETRY_MS;
                    if (n>=2 && !memcmp(reply,prefix,2) && dpf_decode(pid,reply+2,(size_t)n-2,&sample)) {
                        frame.known|=sample.known;
                        frame.supported=(frame.supported & ~mask)|sample.supported;
                        frame.valid|=sample.valid;
                        due[group]=started+DPF_PERIOD_MS;
                        for (unsigned field=0;field<DPF_COUNT;++field) {
                            if (!(sample.valid & DPF_BIT(field))) continue;
                            frame.value[field]=sample.value[field];observed[field]=started;
                        }
                    } else if (n==-1) {
                        frame.valid=0;
                        diag_link_drop(&link);
                        if (diag_link_lost(&link)) diag_running=0;
                    }
                } else if (supported==-1) {
                    frame.valid=0;
                    diag_link_drop(&link);
                    if (diag_link_lost(&link)) diag_running=0;
                }
                /* A newly acquired bitmap consumes this tick; no immediate data
                 * read. Each worker issues at most one diagnostic request/tick. */
            }
            (void)diag_link_should_yield(&link,bd_mono_ms());
        }
        frame.sent_ms=bd_mono_ms();
        if (++frame.seq==0) ++frame.seq;
        for (unsigned field=0;field<DPF_COUNT;++field) {
            const uint64_t age=frame.sent_ms>=observed[field] ? frame.sent_ms-observed[field] : 999999;
            frame.age_ms[field]=(unsigned)(age>999999 ? 999999 : age);
        }
        {
            char packet[DPF_MAX_PACKET];
            const int size=dpf_encode(&frame,packet,sizeof packet);
            if (size>0) (void)sendto(sock,packet,(size_t)size,0,(const struct sockaddr*)&peer,sizeof peer);
        }
        ++count;
        if (diag_running && (!cycles || count<cycles)) poll(0,0,250);
    }
    diag_link_close(&link);
    close(sock);
    return 0;
}
