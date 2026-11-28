#include "obd_capabilities.h"
#include "diag_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t now = 100;
static unsigned calls, expected_page;
static int result = 6;
static unsigned char response[] = {0x41,0,0,0,0,1};
uint64_t bd_mono_ms(void) { return now; }

int diag_link_request(struct diag_link *link, unsigned ecu, const unsigned char *request,
                      unsigned n, const unsigned char *prefix, unsigned pn,
                      unsigned char *reply, unsigned cap, unsigned timeout, unsigned *negative) {
    assert(link && ecu == 0x12 && n == 2 && pn == 2 && cap >= 6 && timeout == 500);
    assert(request[0] == 1 && request[1] == expected_page);
    assert(prefix[0] == 0x41 && prefix[1] == expected_page);
    ++calls;
    *negative = result == -2 ? 0x12 : 0;
    memcpy(reply,response,sizeof response);
    return result;
}

int main(void) {
    struct diag_link link;
    struct obd_capabilities caps;
    memset(&link,0,sizeof link);
    memset(&caps,0,sizeof caps);
    assert(obd_pid_status(&caps,0x5c)==OBD_CAPABILITY_PENDING);
    assert(obd_pid_advertised(&caps,&link,0x5c) == OBD_CAPABILITY_PENDING);
    expected_page = response[1] = 0x20;
    assert(obd_pid_advertised(&caps,&link,0x5c) == OBD_CAPABILITY_PENDING);
    expected_page = response[1] = 0x40;
    response[5] = 0x14; /* oil 5C and fuel 5E, not the continuation bit */
    assert(obd_pid_advertised(&caps,&link,0x5c) == 1);
    assert(obd_pid_advertised(&caps,&link,0x5e) == 1);
    assert(obd_pid_advertised(&caps,&link,0x42) == 0);
    assert(calls == 3); /* retained across short leases */

    /* Every bit, including the MSB, maps to base+1 through base+31. */
    for (unsigned page = 0; page < OBD_CAPABILITY_PAGES; ++page) {
        caps.known = 15;
        for (unsigned bit = 1; bit < 32; ++bit) {
            for (unsigned p=0;p<OBD_CAPABILITY_PAGES;++p) caps.bits[p]=1;
            caps.bits[page] |= UINT32_C(1) << (32-bit);
            for (unsigned pid = 1; pid < 32; ++pid)
                assert(obd_pid_advertised(&caps,&link,page*32+pid) == (pid == bit) &&
                       obd_pid_status(&caps,page*32+pid)==(pid==bit));
        }
    }
    assert(calls == 3);
    for (unsigned pid = 0; pid < 0x100; pid += 32)
        assert(obd_pid_advertised(&caps,&link,pid) == 0);
    assert(obd_pid_advertised(&caps,&link,0x81) == 0);
    assert(obd_pid_advertised(0,&link,5) == 0);
    assert(obd_pid_advertised(&caps,0,5) == 0);

    /* No continuation means no higher-page request, even if it would answer. */
    memset(&caps,0,sizeof caps);
    expected_page = response[1] = response[5] = 0;
    assert(obd_pid_advertised(&caps,&link,0x5c) == OBD_CAPABILITY_PENDING);
    assert(obd_pid_advertised(&caps,&link,0x5c) == 0);
    assert(calls == 4);

    /* Malformed, refused, lost and busy reads never become capability evidence.
     * A fresh retry can recover; retries within five seconds send nothing. */
    const int results[] = {2,5,7,-2,-1,-3};
    for (unsigned i = 0; i < sizeof results / sizeof results[0]; ++i) {
        memset(&caps,0,sizeof caps);
        result = results[i];
        int rc = obd_pid_advertised(&caps,&link,5);
        assert(rc == (result == -1 || result == -3 ? result : OBD_CAPABILITY_PENDING));
        unsigned before = calls;
        assert(caps.known == 0);
        now += OBD_CAPABILITY_RETRY_MS-1;
        assert(obd_pid_advertised(&caps,&link,5) == OBD_CAPABILITY_PENDING);
        assert(calls == before);
        ++now;
        result = 6;
        response[2] = 0x08; /* coolant PID 05 */
        assert(obd_pid_advertised(&caps,&link,5) == 1);
        assert(calls == before+1);
    }
    memset(&caps,0,sizeof caps);
    response[1] = 0x20; /* mismatched prefix */
    assert(obd_pid_advertised(&caps,&link,5) == OBD_CAPABILITY_PENDING);
    assert(!caps.known);
    puts("obd-capabilities: bit order, bounds, chain, cache, malformed/NRC/timeout/busy/retry checks passed");
    return 0;
}
