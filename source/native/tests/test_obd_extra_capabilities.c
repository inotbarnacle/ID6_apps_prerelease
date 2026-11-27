#include "obd_capabilities_extra.h"
#include "diag_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls,expected;
static uint64_t now=100;
static int result=6,wrong_prefix;
static uint32_t masks[]={1,1,1,1,UINT32_C(0x88200002)};
uint64_t bd_mono_ms(void) { return now; }
int diag_link_request(struct diag_link *link,unsigned ecu,const unsigned char *request,unsigned n,
                      const unsigned char *prefix,unsigned pn,unsigned char *reply,unsigned cap,
                      unsigned timeout,unsigned *negative) {
    assert(link && ecu==0x12 && n==2 && pn==2 && cap>=6 && timeout==500);
    assert(request[0]==1 && request[1]==expected && !(request[1]&31) && request[1]<=0x80);
    assert(prefix[0]==0x41 && prefix[1]==request[1]);
    ++calls;*negative=0x31;
    const uint32_t mask=masks[request[1]/32];
    reply[0]=0x41;reply[1]=wrong_prefix?0:request[1];
    for (unsigned i=0;i<4;++i) reply[i+2]=(unsigned char)(mask>>(24-8*i));
    return result;
}
static void reset(struct obd_capabilities *standard,struct obd_extra_capabilities *extra) {
    memset(standard,0,sizeof *standard);memset(extra,0,sizeof *extra);
    standard->known=15;
    for (unsigned i=0;i<4;++i) standard->bits[i]=1;
}
int main(void) {
    struct diag_link link;struct obd_capabilities standard;struct obd_extra_capabilities extra;
    memset(&link,0,sizeof link);memset(&standard,0,sizeof standard);memset(&extra,0,sizeof extra);
    assert(obd_pid_advertised(&standard,&link,0x85)==0 && !calls);
    for (unsigned page=0;page<5;++page) {
        expected=32*page;const unsigned before=calls;
        assert(obd_pid_advertised_extra(&standard,&extra,&link,0x85)==OBD_CAPABILITY_PENDING);
        assert(calls==before+1);
    }
    assert(standard.known==15 && extra.known && calls==5);
    for (unsigned pid=0x81;pid<=0x9f;++pid) {
        const int advertised=(masks[4] & (UINT32_C(1)<<(32-(pid-0x80))))!=0;
        assert(obd_pid_advertised_extra(&standard,&extra,&link,pid)==advertised && calls==5);
    }
    assert(obd_pid_status(&standard,0x7f)==0 && obd_pid_advertised(&standard,&link,0x85)==0 && calls==5);
    for (unsigned pid=0;pid<=0x100;++pid)
        if (pid<0x81 || pid>0x9f) assert(obd_pid_advertised_extra(&standard,&extra,&link,pid)==0 && calls==5);
    for (unsigned missing=0;missing<4;++missing) {
        reset(&standard,&extra);standard.bits[missing]=0;const unsigned before=calls;
        assert(obd_pid_advertised_extra(&standard,&extra,&link,0x85)==0 && calls==before && !extra.known);
    }
    const int failures[]={2,5,7,-2,-1,-3};
    for (unsigned i=0;i<sizeof failures/sizeof failures[0];++i) {
        reset(&standard,&extra);expected=0x80;result=failures[i];
        assert(obd_pid_advertised_extra(&standard,&extra,&link,0x85)==
               (result==-1||result==-3?result:OBD_CAPABILITY_PENDING));
        assert(!extra.known && standard.known==15);const unsigned before=calls;
        now+=4999;
        assert(obd_pid_advertised_extra(&standard,&extra,&link,0x85)==OBD_CAPABILITY_PENDING && calls==before);
        ++now;result=6;
        assert(obd_pid_advertised_extra(&standard,&extra,&link,0x85)==OBD_CAPABILITY_PENDING && extra.known && calls==before+1);
        assert(obd_pid_advertised_extra(&standard,&extra,&link,0x85)==1 && calls==before+1);
    }
    reset(&standard,&extra);wrong_prefix=1;
    assert(obd_pid_advertised_extra(&standard,&extra,&link,0x85)==OBD_CAPABILITY_PENDING && !extra.known);
    assert(obd_pid_advertised_extra(0,&extra,&link,0x85)==0 && obd_pid_advertised_extra(&standard,0,&link,0x85)==0 &&
           obd_pid_advertised_extra(&standard,&extra,0,0x85)==0);
    puts("Explicit bitmap-80 extension: range/bit order, complete continuation, default exclusion, cache and bounded failures passed.");
}
