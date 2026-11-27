#include "obd_capabilities.h"
#include "diag_link.h"
#include "gauge_core.h"
#include <string.h>

int obd_pid_advertised(struct obd_capabilities *caps, struct diag_link *link, unsigned pid) {
    unsigned page, wanted;
    if (!caps || !link || !pid || pid > 0x60 || !(pid & 0x1f)) return 0;
    wanted = (pid - 1) / 32;
    for (page = 0; page <= wanted; ++page) {
        const unsigned flag = 1u << page;
        if (!(caps->known & flag)) {
            const unsigned char request[] = {0x01, (unsigned char)(page * 32)};
            const unsigned char prefix[] = {0x41, request[1]};
            unsigned char reply[16];
            unsigned negative;
            uint64_t now = bd_mono_ms();
            int n;
            if (now < caps->retry_at[page]) return OBD_CAPABILITY_PENDING;
            n = diag_link_request(link, 0x12, request, sizeof request, prefix, sizeof prefix,
                                  reply, sizeof reply, 500, &negative);
            if (n != 6 || memcmp(reply, prefix, sizeof prefix)) {
                caps->retry_at[page] = bd_mono_ms() + OBD_CAPABILITY_RETRY_MS;
                return n == -1 || n == -3 ? n : OBD_CAPABILITY_PENDING;
            }
            caps->bits[page] = ((uint32_t)reply[2] << 24) | ((uint32_t)reply[3] << 16) |
                               ((uint32_t)reply[4] << 8) | reply[5];
            caps->known |= flag;
            if (page < wanted) return OBD_CAPABILITY_PENDING; /* next call discovers the next page */
        }
        if (page < wanted && !(caps->bits[page] & 1u)) return 0;
    }
    return (caps->bits[wanted] & (UINT32_C(1) << (32 - (pid - wanted * 32)))) != 0;
}
