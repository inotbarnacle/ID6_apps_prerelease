#include "dpf_regen_capabilities.h"
#include "diag_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls,expected;
static uint64_t now=100;
static int result=6,wrong_prefix;
static uint32_t masks[]={1,1,1,1,UINT32_C(1)<<21};
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

static void lower_known(struct obd_capabilities *standard,struct dpf_regen_capabilities *regen) {
    memset(standard,0,sizeof *standard);memset(regen,0,sizeof *regen);
    standard->known=15;
    for (unsigned i=0;i<4;++i) standard->bits[i]=1;
}

int main(void) {
    struct diag_link link;struct obd_capabilities standard;struct dpf_regen_capabilities regen;
    memset(&link,0,sizeof link);memset(&standard,0,sizeof standard);memset(&regen,0,sizeof regen);
    assert(obd_pid_advertised(&standard,&link,0x8b)==0 && !calls); /* default policy is still four pages */
    for (unsigned page=0;page<5;++page) {
        expected=32*page;const unsigned before=calls;
        int state=dpf_regen_advertised(&standard,&regen,&link);
        assert(calls==before+1 && state==(page<4?OBD_CAPABILITY_PENDING:1));
    }
    assert(dpf_capability_generation(&standard,&regen)==31);
    assert(dpf_regen_advertised(&standard,&regen,&link)==1 && calls==5);
    assert(obd_pid_advertised(&standard,&link,0x8b)==0 && calls==5);
    assert(obd_pid_status(&standard,0x7f)==0); /* 7F itself was never advertised or measured */

    for (unsigned missing=0;missing<4;++missing) {
        lower_known(&standard,&regen);standard.bits[missing]=0;
        const unsigned before=calls;
        assert(dpf_regen_advertised(&standard,&regen,&link)==0 && calls==before && !regen.known);
    }
    lower_known(&standard,&regen);expected=0x80;masks[4]=0;
    assert(dpf_regen_advertised(&standard,&regen,&link)==0 && regen.known);
    masks[4]=UINT32_C(1)<<21;

    const int failures[]={2,5,7,-2,-1,-3};
    for (unsigned i=0;i<sizeof failures/sizeof failures[0];++i) {
        lower_known(&standard,&regen);expected=0x80;result=failures[i];
        int state=dpf_regen_advertised(&standard,&regen,&link);
        assert(state==(result==-1||result==-3?result:OBD_CAPABILITY_PENDING));
        assert(!regen.known && standard.known==15);
        const unsigned before=calls;
        now+=4999;
        assert(dpf_regen_advertised(&standard,&regen,&link)==OBD_CAPABILITY_PENDING && calls==before);
        ++now;result=6;
        assert(dpf_regen_advertised(&standard,&regen,&link)==1 && calls==before+1);
    }
    lower_known(&standard,&regen);wrong_prefix=1;
    assert(dpf_regen_advertised(&standard,&regen,&link)==OBD_CAPABILITY_PENDING && !regen.known);
    wrong_prefix=0;
    assert(dpf_regen_advertised(0,&regen,&link)==0 && dpf_regen_advertised(&standard,0,&link)==0 &&
           dpf_regen_advertised(&standard,&regen,0)==0);
    puts("Optional 8B capability extension: exact continuation, one bitmap per call, default policy/cache and bounded failures passed");
}
