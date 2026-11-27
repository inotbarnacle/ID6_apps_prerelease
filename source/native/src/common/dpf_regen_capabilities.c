#include "dpf_regen_capabilities.h"
#include "diag_link.h"
#include "gauge_core.h"
#include <string.h>

int dpf_regen_advertised(struct obd_capabilities *standard,struct dpf_regen_capabilities *regen,
                        struct diag_link *link) {
    unsigned before,page;
    int result;
    if (!standard || !regen || !link) return 0;
    before=standard->known;
    /* Discover through bitmap 60 using the existing helper. This does not
     * read measurement 7F, and support of 7F itself is irrelevant here. */
    result=obd_pid_advertised(standard,link,0x7f);
    if (result==-1 || result==-3) return result;
    for (page=0;page<OBD_CAPABILITY_PAGES;++page) {
        if (!(standard->known & (1u<<page))) return OBD_CAPABILITY_PENDING;
        if (!(standard->bits[page]&1u)) return 0;
    }
    if (standard->known!=before) return OBD_CAPABILITY_PENDING;
    if (!regen->known) {
        const unsigned char request[]={1,0x80},prefix[]={0x41,0x80};
        unsigned char reply[16];unsigned negative;
        if (bd_mono_ms()<regen->retry_at) return OBD_CAPABILITY_PENDING;
        result=diag_link_request(link,0x12,request,sizeof request,prefix,sizeof prefix,
                                 reply,sizeof reply,500,&negative);
        if (result!=6 || memcmp(reply,prefix,sizeof prefix)) {
            regen->retry_at=bd_mono_ms()+OBD_CAPABILITY_RETRY_MS;
            return result==-1 || result==-3 ? result : OBD_CAPABILITY_PENDING;
        }
        regen->bits=((uint32_t)reply[2]<<24)|((uint32_t)reply[3]<<16)|((uint32_t)reply[4]<<8)|reply[5];
        regen->known=1;
    }
    return (regen->bits & (UINT32_C(1)<<21))!=0;
}
