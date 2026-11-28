#ifndef GAUGE_COEXISTENCE_H
#define GAUGE_COEXISTENCE_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { BOOST_ACTIVE_PORT = 42871, SHIFT_ACTIVE_PORT, LAUNCH_ACTIVE_PORT,
       BOOST_MOST_PORT = 42875, SHIFT_MOST_PORT, LAUNCH_MOST_PORT,
       WARM_ACTIVE_PORT, WARM_MOST_PORT };
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

static inline int native_android_auto_installed(void) {
#ifdef __QNX__
    return access("/opt/aa/nbt-aad", F_OK) == 0 ||
        access("/opt/hmi/ID5/data/ro/Plugins/NbtAad/NbtAad.manifest", F_OK) == 0 ||
        access("/var/opt/hmi/data/ID5/Plugins/NbtAad/NbtAad.manifest", F_OK) == 0;
#else
    return 0;
#endif
}

#endif
