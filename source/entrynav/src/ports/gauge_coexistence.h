#ifndef GAUGE_COEXISTENCE_H
#define GAUGE_COEXISTENCE_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { SHIFT_ACTIVE_PORT = 42872, LAUNCH_ACTIVE_PORT = 42873, WARM_ACTIVE_PORT = 42878 };
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

#endif
