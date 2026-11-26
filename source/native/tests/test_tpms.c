#include "tpms.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    int hpa[4] = {-2,-2,-2,-2};
    const unsigned char ok[] = {0x09,0xc4,0x09,0xd6,0x0a,0x28,0x0a,0x3c};
    assert(tpms_decode(ok, sizeof ok, hpa));
    assert(hpa[0]==2500 && hpa[1]==2518 && hpa[2]==2600 && hpa[3]==2620);
    const unsigned char flat[] = {0,0,0,0,0,0,0,0};
    assert(tpms_decode(flat, sizeof flat, hpa));
    assert(hpa[0]==0 && hpa[1]==0 && hpa[2]==0 && hpa[3]==0);
    const unsigned char ceiling[] = {0x17,0x70,0x17,0x70,0x17,0x70,0x17,0x70};
    assert(tpms_decode(ceiling, sizeof ceiling, hpa));
    assert(hpa[0]==6000 && hpa[1]==6000 && hpa[2]==6000 && hpa[3]==6000);
    const unsigned char over[] = {0x17,0x71,0,0,0,0,0,0};
    assert(!tpms_decode(over, sizeof over, hpa));
    const unsigned char sentinel[] = {0x09,0xc4,0xff,0xff,0x0a,0x28,0x0a,0x3c};
    assert(!tpms_decode(sentinel, sizeof sentinel, hpa));
    int keep[4] = {1000,1001,1002,1003};
    const unsigned char late_fail[] = {0x09,0xc4,0x09,0xd6,0xff,0xff,0x0a,0x3c};
    assert(!tpms_decode(late_fail, sizeof late_fail, keep));
    assert(keep[0]==1000 && keep[1]==1001 && keep[2]==1002 && keep[3]==1003);
    assert(!tpms_decode(sentinel, sizeof sentinel, keep));
    assert(keep[0]==1000 && keep[1]==1001 && keep[2]==1002 && keep[3]==1003);
    assert(!tpms_decode(ok, 7, hpa));
    assert(!tpms_decode(NULL, 8, hpa));
    assert(!tpms_decode(ok, 8, NULL));
    puts("tpms ok");
    return 0;
}
