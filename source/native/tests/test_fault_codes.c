#include "fault_codes.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    unsigned char reply[FAULT_REPLY_MAX]={0x59,2,0xaf,0,0,1,0x29,0x24,0x57,0,0x20};
    struct fault_reply parsed;
    struct fault_record record;
    assert(fault_parse_reply(reply,11,&parsed));
    assert(parsed.available==0xaf && parsed.count==2);
    assert(fault_record_at(&parsed,0,&record) && record.code==1 && record.status==0x29 && record.available==0xaf);
    assert(fault_record_at(&parsed,1,&record) && record.code==0x245700 && record.status==0x20);
    assert(!fault_record_at(&parsed,2,&record));
    assert(!fault_record_at(&parsed,(size_t)-1,&record));
    assert(!fault_record_at(0,0,&record) && !fault_record_at(&parsed,0,0));
    assert(!fault_parse_reply(0,3,&parsed) && !fault_parse_reply(reply,3,0));
    assert(!fault_parse_reply(reply,FAULT_REPLY_MAX+1u,&parsed));
    assert(!fault_parse_reply(reply,(size_t)-1,&parsed));
    for (size_t length=0;length<=sizeof reply;++length) {
        const int expected=length>=3 && (length-3)%4==0;
        assert(fault_parse_reply(reply,length,&parsed)==expected);
        if (expected) assert(parsed.count==(length-3)/4);
        else assert(!parsed.records && parsed.count==0 && parsed.available==0);
    }
    reply[0]=0x7f;
    assert(!fault_parse_reply(reply,11,&parsed));
    reply[0]=0x59; reply[1]=1;
    assert(!fault_parse_reply(reply,11,&parsed));
    reply[1]=2; reply[2]=0;
    assert(fault_parse_reply(reply,3,&parsed) && parsed.count==0 && parsed.available==0);
    reply[2]=8; reply[3]=reply[4]=reply[5]=255; reply[6]=9;
    assert(fault_parse_reply(reply,7,&parsed) && fault_record_at(&parsed,0,&record));
    assert(record.code==0xffffff && record.status==9 && (record.status & record.available)==8);
    puts("UDS fault response prefix/length, 24-bit code, availability, bounds and complete-record checks passed.");
    return 0;
}
