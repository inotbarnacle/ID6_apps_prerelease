#include "boost_defaults.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void advertise(struct obd_capabilities *caps, unsigned pid) {
    unsigned page=(pid-1)/32;
    caps->known|=1u<<page;
    caps->bits[page]|=UINT32_C(1)<<(32-(pid-page*32));
}

int main(void) {
    struct obd_capabilities caps={{0},{0},0};
    bd_mask known, supported;
    unsigned char slots[BD_DEFAULT_SLOTS];
    bd_diesel_masks(&caps,0,0,&known,&supported);
    assert(!known && !supported);
    bd_diesel_masks(0,0,0,&known,&supported);
    assert(!known && !supported);
    bd_diesel_slots(0,slots);
    assert(!bd_slots_mask(slots));
    for (unsigned i=0;i<BD_DEFAULT_SLOTS;++i) assert(slots[i]==BD_EMPTY_SLOT);

    advertise(&caps,0x05); advertise(&caps,0x0b); advertise(&caps,0x0c);
    advertise(&caps,0x20);
    bd_diesel_masks(&caps,0,0,&known,&supported);
    assert((supported & ((1u<<BD_COOLANT)|(1u<<BD_MAP)|(1u<<BD_RPM)))==
                        ((1u<<BD_COOLANT)|(1u<<BD_MAP)|(1u<<BD_RPM)));
    assert(!(known & (1u<<BD_BOOST))); // MAP alone cannot establish boost support
    assert(!(known & (1u<<BD_OIL_TEMP)));
    assert(!(known & (1u<<BD_TRANS)));
    advertise(&caps,0x33); advertise(&caps,0x40);
    bd_diesel_masks(&caps,1,0,&known,&supported);
    assert((supported & ((1u<<BD_BOOST)|(1u<<BD_TRANS)))==((1u<<BD_BOOST)|(1u<<BD_TRANS)));
    assert(!(known & (1u<<BD_OIL_TEMP)));
    caps.known|=4;
    bd_diesel_masks(&caps,1,0,&known,&supported);
    assert(known==bd_diesel_candidates() && !(supported & (1u<<BD_OIL_TEMP)));
    advertise(&caps,0x5c);
    bd_diesel_masks(&caps,1,0,&known,&supported);
    assert(supported & (1u<<BD_OIL_TEMP));

    // Every candidate subset produces unique supported gauges only, at most nine.
    unsigned indices[BD_COUNT], count=0;
    for (unsigned i=0;i<BD_COUNT;++i) if (bd_diesel_candidates() & BD_BIT(i)) indices[count++]=i;
    for (unsigned subset=0;subset<(1u<<count);++subset) {
        bd_mask mask=0, seen=0;
        unsigned displayed=0, available=0, egt_available=0;
        for (unsigned i=0;i<count;++i) if (subset & (1u<<i)) {
            mask|=BD_BIT(indices[i]);
            if (indices[i]>=BD_EGT_B1S1) egt_available=1;
            else ++available;
        }
        available+=egt_available;
        bd_diesel_slots(mask,slots);
        for (unsigned i=0;i<BD_DEFAULT_SLOTS;++i) {
            if (slots[i]==BD_EMPTY_SLOT) continue;
            bd_mask bit=BD_BIT(slots[i]);
            assert((mask&bit) && !(seen&bit)); seen|=bit; ++displayed;
        }
        assert(displayed==(available>9 ? 9 : available));
        assert(bd_slots_mask(slots)==seen);
        if (mask & (1u<<BD_RPM)) assert(slots[8]==BD_RPM);
    }

    bd_cap_frame report={UINT64_MAX,12345,123,bd_diesel_candidates(),1u<<BD_RAIL}, parsed;
    bd_frame data, decoded;
    char packet[BD_AUTO_PACKET], legacy[BD_MAX_PACKET];
    memset(&data,0,sizeof data);
    data.sent_ms=report.sent_ms; data.valid=1u<<BD_RAIL; data.value[BD_RAIL]=1000;
    int size=bd_auto_encode(&report,&data,packet,sizeof packet);
    assert(size>0 && size<BD_AUTO_PACKET);
    assert(bd_auto_parse(packet,(size_t)size,&parsed,&decoded));
    assert(parsed.token==report.token && parsed.worker_pid==123 && parsed.known==report.known);
    assert(decoded.value[BD_RAIL]==1000 && decoded.valid==data.valid);
    assert(!bd_parse(packet,(size_t)size,&decoded)); // old readers safely reject the envelope
    int n=bd_encode(&data,legacy,sizeof legacy);
    assert(n>0 && bd_parse(legacy,(size_t)n,&decoded));
    assert(!bd_auto_parse(legacy,(size_t)n,&parsed,&decoded));
    assert(bd_auto_encode(&report,&data,packet,20)<0);
    ++data.sent_ms;
    assert(bd_auto_encode(&report,&data,packet,sizeof packet)<0);
    data.sent_ms=report.sent_ms;
    data.demo=1;
    assert(bd_auto_encode(&report,&data,packet,sizeof packet)<0);
    data.demo=0;
    size=bd_auto_encode(&report,&data,packet,sizeof packet);
    for (int i=0;i<size-1;++i) assert(!bd_auto_parse(packet,(size_t)i,&parsed,&decoded));
    // Exact lengths, lexical bounds and capability subset validation.
    const char *bad[]={"NBC1 -1 1 1 0 0\n","NBC1 +1 1 1 0 0\n","NBC1 18446744073709551616 1 1 0 0\n",
        "NBC1 1 4294967297 1 0 0\n","NBC1 1 0 1 0 0\n","NBC1 1 1 0 0 0\n",
        "NBC1 1 1 1 4294967296 0\n","NBC1 1 1 1 0 1\n","NBC1 1 1 1 64 64\n",
        "NBC1 1 1 1 0x20 0\n","NBC1 1 1 1 0 0 garbage\n"};
    for (unsigned i=0;i<sizeof bad/sizeof bad[0];++i)
        assert(!bd_cap_parse(bad[i],strlen(bad[i]),&parsed));
    packet[10]=0;
    assert(!bd_auto_parse(packet,(size_t)size,&parsed,&decoded));
    puts("Automatic diesel candidate subsets, cached capability states, strict envelopes and legacy packets passed.");
}
