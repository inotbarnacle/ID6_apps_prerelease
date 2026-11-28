#include "obd_capabilities_extra.h"
#include "diag_link.h"
#include "gauge_core.h"
#include <string.h>

int obd_pid_advertised_extra(struct obd_capabilities *standard,struct obd_extra_capabilities *extra,
                             struct diag_link *link,unsigned pid) {
    unsigned page,before;
    int result;
    if (!standard || !extra || !link || pid<0x81 || pid>0x9f) return 0;
    before=standard->known;
    /* This helper reads bitmap pages only. Measurement 7F itself is irrelevant. */
    result=obd_pid_advertised(standard,link,0x7f);
    if (result==-1 || result==-3) return result;
    for (page=0;page<OBD_CAPABILITY_PAGES;++page) {
        if (!(standard->known & (1u<<page))) return OBD_CAPABILITY_PENDING;
        if (!(standard->bits[page]&1u)) return 0;
    }
    if (before!=standard->known) return OBD_CAPABILITY_PENDING;
    if (!extra->known) {
        const unsigned char request[]={1,0x80},prefix[]={0x41,0x80};
        unsigned char reply[16];unsigned negative;
        if (bd_mono_ms()<extra->retry_at) return OBD_CAPABILITY_PENDING;
        result=diag_link_request(link,0x12,request,sizeof request,prefix,sizeof prefix,
                                 reply,sizeof reply,500,&negative);
        if (result!=6 || memcmp(reply,prefix,sizeof prefix)) {
            extra->retry_at=bd_mono_ms()+OBD_CAPABILITY_RETRY_MS;
            return result==-1 || result==-3 ? result : OBD_CAPABILITY_PENDING;
        }
        extra->bits=((uint32_t)reply[2]<<24)|((uint32_t)reply[3]<<16)|((uint32_t)reply[4]<<8)|reply[5];
        extra->known=1;
        return OBD_CAPABILITY_PENDING;
    }
    return (extra->bits & (UINT32_C(1)<<(32-(pid-0x80))))!=0;
}
