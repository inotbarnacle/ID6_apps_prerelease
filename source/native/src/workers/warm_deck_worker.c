#define _POSIX_C_SOURCE 200809L
#include "diag_link.h"
#include "gauge_coexistence.h"
#include "gauge_core.h"
#include "warm_frame.h"
#include <poll.h>
#include <stdlib.h>
#include <string.h>

static int temperature(struct diag_link *link, unsigned signal, double *out) {
    static const unsigned char requests[][3]={{1,0x5c,0},{1,5,0},{0x22,0xda,0x12}};
    static const unsigned char prefixes[][3]={{0x41,0x5c,0},{0x41,5,0},{0x62,0xda,0x12}};
    unsigned char reply[16];
    unsigned negative;
    const unsigned length=signal==WARM_TRANS ? 3 : 2;
    const int n=diag_link_request(link,signal==WARM_TRANS ? 0x18 : 0x12,
        requests[signal],length,prefixes[signal],length,reply,sizeof reply,450,&negative);
    if (n<0) return n;
    if ((unsigned)n<=length) return 0;
    if (signal==WARM_TRANS) return bd_decode_did(0xda12,reply+length,n-length,out);
    return bd_decode_obd(requests[signal][1],reply+length,n-length,out);
}

int main(int argc, char **argv) {
    struct diag_link_cfg cfg={0,6801,DIAG_AUTO,DIAG_LOOP,"warmdeck",1};
    struct diag_link link;
    unsigned cycles=0, count=0, next=0;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && i+1<argc && !cfg.host) cfg.host=argv[++i];
        else if ((!strcmp(argv[i],"--port") || !strcmp(argv[i],"--cycles")) && i+1<argc) {
            const int port=!strcmp(argv[i],"--port");
            char *end;
            unsigned long value=strtoul(argv[++i],&end,10);
            if (*end || !value || value>(port ? 65535u : 1000u)) return 2;
            if (port) cfg.port=(unsigned)value;
            else cycles=(unsigned)value;
        } else return 2;
    }
    const pid_t parent=getppid();
    const int socket_fd=gauge_bind(WARM_ACTIVE_PORT);
    if (socket_fd<0 || parent<=1) return 1;
    const struct sockaddr_in peer=gauge_local_address(WARM_PORT);
    struct warm_frame frame;
    memset(&frame,0,sizeof frame);
    diag_link_signals();
    diag_link_init(&link,&cfg);
    while (diag_running && getppid()==parent && (!cycles || count<cycles)) {
        if (diag_link_up(&link)) {
            diag_link_tick(&link);
            for (unsigned reads=0;reads<WARM_COUNT && diag_running;++reads) {
                const unsigned signal=next;
                next=(next+1)%WARM_COUNT;
                const int status=temperature(&link,signal,&frame.celsius[signal]);
                frame.sample[signal]=status==1 ? bd_mono_ms() : 0;
                if (status==-1) {
                    diag_link_drop(&link);
                    if (diag_link_lost(&link)) diag_running=0;
                    break;
                }
                if (diag_link_should_yield(&link,bd_mono_ms())) break;
            }
        }
        (void)sendto(socket_fd,&frame,sizeof frame,0,(const struct sockaddr*)&peer,sizeof peer);
        ++count;
        if (diag_running && !diag_link_on_most(&link)) diag_link_drop(&link);
        if (diag_running && (!cycles || count<cycles)) poll(0,0,1000);
    }
    diag_link_close(&link);
    close(socket_fd);
    return 0;
}
