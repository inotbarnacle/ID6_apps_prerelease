#include "obd_fault_codes.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* Independent protocol examples: ELM327DSJ p34 and synthetic CAN framing. */
    const struct { uint16_t value; const char* text; } examples[]={
        {0x0133,"P0133"},{0xd016,"U1016"},{0x1131,"P1131"},
        {0x4001,"C0001"},{0x8001,"B0001"},{0xffff,"U3FFF"},{0x0001,"P0001"}
    };
    for (unsigned i=0;i<sizeof examples/sizeof examples[0];++i) {
        char text[7]; memset(text,'!',sizeof text);
        obd_fault_format(examples[i].value,text);
        assert(!strcmp(text,examples[i].text) && text[6]=='!');
    }
    for (unsigned service=0;service<256;++service) {
        unsigned char reply[]={0x43,2,0x01,0x33,0xd0,0x16,0,0};
        struct obd_fault_reply parsed;
        uint16_t code;
        reply[0]=(unsigned char)(0x40u|service);
        assert(obd_fault_parse_reply(reply,sizeof reply,service,&parsed)==obd_fault_service(service));
        if (!obd_fault_service(service)) { assert(!parsed.records && !parsed.count); continue; }
        assert(parsed.count==2);
        assert(obd_fault_record_at(&parsed,0,&code) && code==0x0133);
        assert(obd_fault_record_at(&parsed,1,&code) && code==0xd016);
        assert(!obd_fault_record_at(&parsed,2,&code));
        assert(!obd_fault_record_at(&parsed,(unsigned)-1,&code));
        assert(!obd_fault_record_at(&parsed,0,0) && !obd_fault_record_at(0,0,&code));
        for (size_t n=0;n<sizeof reply;++n)
            assert(obd_fault_parse_reply(reply,n,service,&parsed)==(n==6));
        reply[7]=1; assert(!obd_fault_parse_reply(reply,sizeof reply,service,&parsed));
        reply[7]=0; reply[1]=3; assert(!obd_fault_parse_reply(reply,sizeof reply,service,&parsed));
        reply[1]=1; assert(!obd_fault_parse_reply(reply,sizeof reply,service,&parsed));
        reply[1]=0; assert(!obd_fault_parse_reply(reply,sizeof reply,service,&parsed));
        memset(reply+2,0,sizeof reply-2);
        assert(obd_fault_parse_reply(reply,sizeof reply,service,&parsed) && !parsed.count);
        reply[1]=1; assert(!obd_fault_parse_reply(reply,sizeof reply,service,&parsed));
    }
    unsigned char full[OBD_FAULT_REPLY_MAX+2]={0x43,255};
    for (unsigned i=0;i<255;++i) { full[2+2*i]=1; full[3+2*i]=(unsigned char)i; }
    struct obd_fault_reply parsed;
    uint16_t code;
    assert(obd_fault_parse_reply(full,OBD_FAULT_REPLY_MAX,3,&parsed) && parsed.count==255);
    assert(obd_fault_record_at(&parsed,254,&code) && code==0x01fe);
    assert(!obd_fault_parse_reply(full,sizeof full,3,&parsed));
    assert(!obd_fault_parse_reply(full,(size_t)-1,3,&parsed));
    assert(!obd_fault_parse_reply(0,2,3,&parsed) && !obd_fault_parse_reply(full,2,3,0));
    assert(!obd_fault_parse_reply(full,OBD_FAULT_REPLY_MAX,7,&parsed));
    const unsigned char legacy[]={0x43,0x01,0x33,0,0,0,0};
    const unsigned char uds[]={0x59,2,0xff,0x01,0x33,0,8};
    assert(!obd_fault_parse_reply(legacy,sizeof legacy,3,&parsed));
    assert(!obd_fault_parse_reply(uds,sizeof uds,3,&parsed));
    assert(!strcmp(obd_fault_state(3),"Confirmed") && !strcmp(obd_fault_state(7),"Pending"));
    assert(!strcmp(obd_fault_state(10),"Permanent") && !strcmp(obd_fault_state(4),"Unknown"));
    puts("OBD counted CAN replies, explicit service, code families, count/filler/size validation and format separation passed.");
    return 0;
}
