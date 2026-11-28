#define _POSIX_C_SOURCE 200809L
#include "diag_link.h"
#include "gauge_coexistence.h"
#include "gauge_core.h"
#include "hybrid_frame.h"
#include <poll.h>
#include <stdlib.h>

/* The iDrive7 eDrive PHEV profile addresses the SME at 0x07. Optional
 * readings stay missing when a vehicle rejects the DID or returns another layout. */
static int read_sme(struct diag_link *link, unsigned did, unsigned width, int is_signed,
                    double factor, double minimum, double maximum, double *value) {
    const unsigned char request[]={0x22,(unsigned char)(did>>8),(unsigned char)did};
    const unsigned char prefix[]={0x62,request[1],request[2]};
    unsigned char reply[64];
    unsigned negative;
    const int count=diag_link_request(link,0x07,request,sizeof request,
        prefix,sizeof prefix,reply,sizeof reply,350,&negative);
    if (count<0) return count;
    if (count<(int)width+3) return 0;
    uint32_t raw=0;
    for (unsigned i=0;i<width;++i) raw=(raw<<8)|reply[3+i];
    const uint64_t range=UINT64_C(1)<<(width*8);
    if (raw==range-1 || (is_signed && (raw==range/2 || raw==range/2-1))) return 0;
    const double decoded=(is_signed && raw>=range/2 ? (double)raw-(double)range : (double)raw)*factor;
    if (decoded<minimum || decoded>maximum) return 0;
    *value=decoded;
    return 1;
}

static int read_obd(struct diag_link *link, unsigned char pid, double *value) {
    const unsigned char request[]={0x01,pid}, prefix[]={0x41,pid};
    unsigned char reply[16];
    unsigned negative;
    const int count=diag_link_request(link,0x12,request,sizeof request,prefix,sizeof prefix,
        reply,sizeof reply,350,&negative);
    if (count<0) return count;
    return count>2 && bd_decode_obd(pid,reply+2,(size_t)count-2,value);
}

static int battery_health(struct diag_link *link, double *value) {
    const unsigned char primary[]={0x22,0x63,0x35}, prefix[]={0x62,0x63,0x35};
    unsigned char reply[64];
    unsigned negative;
    const int count=diag_link_request(link,0x07,primary,sizeof primary,prefix,sizeof prefix,
        reply,sizeof reply,350,&negative);
    // OBDb's PHEV definition uses the fourth data byte of 6335.
    if (count>=7 && reply[6]>=1 && reply[6]<=100) {
        *value=reply[6];
        return 1;
    }
    if (count==-1 || (count==-2 && negative!=0x31 && negative!=0x11 && negative!=0x12)) return count;
    // iDrive7 eDrive also documents DD7B as a one-byte health percentage.
    const unsigned char alternate[]={0x22,0xdd,0x7b}, alternate_prefix[]={0x62,0xdd,0x7b};
    const int n=diag_link_request(link,0x07,alternate,sizeof alternate,
        alternate_prefix,sizeof alternate_prefix,reply,sizeof reply,350,&negative);
    if (n!=4 || reply[3]<1 || reply[3]>110) return n<0 ? n : 0;
    *value=reply[3];
    return 1;
}

static int read_signal(struct diag_link *link, unsigned signal, unsigned *voltage_did, double *value) {
    switch (signal) {
    case HYBRID_SOC: return read_sme(link,0xddbc,2,0,0.1,0.5,100,value);
    case HYBRID_VOLTS: {
        int result=read_sme(link,*voltage_did,2,0,0.01,150,500,value);
        if ((result==0 || result==-2) && *voltage_did==0xddb4) {
            result=read_sme(link,0xdd68,2,0,0.01,150,500,value);
            if (result==1) *voltage_did=0xdd68;
        }
        return result;
    }
    case HYBRID_AMPS: return read_sme(link,0xdd69,4,1,0.01,-800,800,value);
    case HYBRID_COOLANT: return read_sme(link,0xdd6c,2,1,0.01,-40,100,value);
    case HYBRID_ENGINE_COOLANT: return read_obd(link,0x05,value);
    case HYBRID_MODULE_VOLTS: return read_obd(link,0x42,value);
    case HYBRID_HEALTH: return battery_health(link,value);
    default: return 0;
    }
}

int main(int argc, char **argv) {
    struct diag_link_cfg cfg={0,6801,DIAG_LOOP};
    struct diag_link link;
    unsigned cycles=0, count=0, next=0, voltage_did=0xddb4;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--host") && i+1<argc && !cfg.host) cfg.host=argv[++i];
        else if ((!strcmp(argv[i],"--port") || !strcmp(argv[i],"--cycles")) && i+1<argc) {
            const int port=!strcmp(argv[i],"--port");
            char *end;
            const unsigned long value=strtoul(argv[++i],&end,10);
            if (*end || !value || value>(port ? 65535u : 1000u)) return 2;
            if (port) cfg.port=(unsigned)value;
            else cycles=(unsigned)value;
        } else return 2;
    }
    if (!cfg.host) return 2;
    const pid_t parent=getppid();
    const int socket_fd=gauge_bind(HYBRID_SOURCE_PORT);
    if (socket_fd<0 || parent<=1) return 1;
    const struct sockaddr_in peer=gauge_local_address(HYBRID_PORT);
    struct hybrid_frame frame={{0},{0}};
    uint64_t next_health=0;
    diag_link_signals();
    diag_link_init(&link,&cfg);
    while (diag_running && getppid()==parent && (!cycles || count<cycles)) {
        if (diag_link_up(&link)) {
            if (next==HYBRID_HEALTH && bd_mono_ms()<next_health) next=0;
            const unsigned signal=next%HYBRID_COUNT;
            next=(signal+1)%HYBRID_COUNT;
            const int status=read_signal(&link,signal,&voltage_did,&frame.value[signal]);
            frame.sample[signal]=status==1 ? bd_mono_ms() : 0;
            if (signal==HYBRID_HEALTH) next_health=bd_mono_ms()+HYBRID_HEALTH_POLL_MS;
            if (status==-1 || signal==HYBRID_COUNT-1) diag_link_drop(&link);
            else (void)diag_link_should_yield(&link,bd_mono_ms());
            ++count;
        } else if (cycles) {
            // Bounded fixture runs also terminate if no gateway answers.
            ++count;
        }
        (void)sendto(socket_fd,&frame,sizeof frame,0,(const struct sockaddr*)&peer,sizeof peer);
        if (diag_running && (!cycles || count<cycles)) poll(0,0,300);
    }
    diag_link_close(&link);
    close(socket_fd);
    return 0;
}
