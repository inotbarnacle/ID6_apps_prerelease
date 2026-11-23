#ifndef GAUGE_COEXISTENCE_H
#define GAUGE_COEXISTENCE_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { HUD_TELEMETRY_PORT = 42870, BOOST_ACTIVE_PORT, SHIFT_ACTIVE_PORT, LAUNCH_ACTIVE_PORT,
       GAUGE_MOST_PORT, BOOST_MOST_PORT, SHIFT_MOST_PORT, LAUNCH_MOST_PORT,
       WARM_ACTIVE_PORT, WARM_MOST_PORT };
enum { HUD_SAMPLE_RPM = 1, HUD_SAMPLE_BOOST = 2 };

struct hud_telemetry {
    uint64_t sampled_ms;
    unsigned source, valid, rpm;
    int boost_tenths;
};

static inline struct sockaddr_in gauge_local_address(unsigned port) {
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(0x7f000001u);
    return address;
}

// The OS releases these sockets on exit, including abnormal worker exits.
static inline int gauge_bind(unsigned port) {
    struct sockaddr_in address = gauge_local_address(port);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) == 0 &&
        bind(fd, (struct sockaddr *)&address, sizeof address) == 0) return fd;
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
}

static inline int gauge_active(unsigned port) {
    int fd = gauge_bind(port);
    if (fd < 0) return 1; // Uncertain: avoid opening a second diagnostic connection.
    close(fd);
    return 0;
}

static inline void gauge_publish(int fd, const struct hud_telemetry *sample) {
    struct sockaddr_in address = gauge_local_address(HUD_TELEMETRY_PORT);
    (void)sendto(fd, sample, sizeof *sample, 0, (struct sockaddr *)&address, sizeof address);
}

// A dashboard that needs MOST asks an existing HUD worker to clear and exit.
static inline void gauge_request_most(void) {
    struct sockaddr_in address = gauge_local_address(GAUGE_MOST_PORT);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        const char request = 1;
        (void)sendto(fd, &request, 1, 0, (struct sockaddr *)&address, sizeof address);
        close(fd);
    }
}

static inline int native_android_auto_installed(void) {
#ifdef __QNX__
    return access("/opt/aa/nbt-aad", F_OK) == 0 ||
        access("/opt/hmi/ID5/data/ro/Plugins/NbtAad/NbtAad.manifest", F_OK) == 0 ||
        access("/var/opt/hmi/data/ID5/Plugins/NbtAad/NbtAad.manifest", F_OK) == 0;
#else
    return 0;
#endif
}

static inline int projection_hud_installed(void) {
    if (native_android_auto_installed()) return 1;
#ifdef __QNX__
    return access("/var/opt/hmi/data/rw/Plugins/CarPlayMenu/librghook.so", F_OK) == 0 ||
        access("/var/opt/hmi/data/ID5/Plugins/CarPlayMenu/librghook.so", F_OK) == 0;
#else
    return 0;
#endif
}

#endif
