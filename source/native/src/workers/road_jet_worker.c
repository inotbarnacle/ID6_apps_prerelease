#ifdef __QNX__
#define _QNX_SOURCE
#endif

#include "road_jet_frame.h"
#include "diag_link.h"
#include "gauge_core.h"

#include <arpa/inet.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* IDs and lengths are specific to the initial BMW diagnostic profile. */
static int read_did(struct diag_link *link, unsigned ecu, unsigned did,
                    unsigned char *reply, unsigned expected) {
    unsigned char request[] = {0x22, did >> 8, did & 255};
    unsigned char response[] = {0x62, did >> 8, did & 255};
    unsigned negative;
    int result = diag_link_request(link, ecu, request, sizeof request, response,
                                   sizeof response, reply, 32, 180, &negative);
    if (result == -1) diag_link_drop(link);
    return result == (int)expected;
}

/* The game needs a fresh speed reading within its low-speed limit. */
static int vehicle_speed(struct diag_link *link) {
    unsigned char reply[32];
    const unsigned char speed[] = {0x01, 0x0d}, match[] = {0x41, 0x0d};
    unsigned negative;
    const int length=diag_link_request(link,0x12,speed,sizeof speed,match,sizeof match,
                                       reply,sizeof reply,180,&negative);
    if (length==-1) diag_link_drop(link);
    return length==3 ? reply[2] : -1;
}

static struct road_jet_frame read_frame(struct diag_link *link) {
    struct road_jet_frame frame = {bd_mono_ms(), ROAD_JET_WAITING, 0, 0};
    unsigned char data[32];

    const int speed=vehicle_speed(link);
    if (speed<0 || speed>ROAD_JET_MAX_KPH) {
        if (speed>ROAD_JET_MAX_KPH) frame.mode=ROAD_JET_TOO_FAST;
        return frame;
    }

    /* DME8FF_R: normalized accelerator pedal position. */
    if (!read_did(link, 0x12, 0x480B, data, 5) &&
        !read_did(link, 0x12, 0x5814, data, 5)) return frame;
    unsigned pedal = data[3] * 256u + data[4];
    if (pedal > 8192) return frame;

    int32_t position = (pedal * 1000u + 4096u) / 8192u;
    frame.started_ms = bd_mono_ms();
    frame.pedal = position;
    frame.mode = ROAD_JET_PEDAL_ONLY;
    if (diag_link_on_most(link)) {
        frame.mode=ROAD_JET_MOST_PEDAL;
        return frame;
    }

    /* EPS_G11: signed pinion angle and a valid, calibrated sensor. */
    if (!read_did(link, 0x30, 0xDB57, data, 10) || data[9] != 3) return frame;
    uint32_t raw = (uint32_t)data[3] << 24 | (uint32_t)data[4] << 16 |
                   (uint32_t)data[5] << 8 | data[6];
    int64_t steering = raw;
    if (raw & 0x80000000u) steering -= INT64_C(4294967296);
    if (steering < -90000 || steering > 90000) return frame;
    /* EPS reports the opposite sign to the frame's right-positive convention. */
    frame.steering = -(int32_t)steering;
    frame.mode = ROAD_JET_ACTIVE;
    return frame;
}

int main(int argc, char **argv) {
    if (argc < 3 || strcmp(argv[1], "--host")) return 2;
    unsigned port = 6801, cycles = 0;
    for (int i = 3; i + 1 < argc; i += 2) {
        const int is_port = !strcmp(argv[i], "--port");
        char *end;
        if (!is_port && strcmp(argv[i], "--cycles")) return 2;
        const unsigned long value = strtoul(argv[i + 1], &end, 10);
        if (*end || !value || value > (is_port ? 65535u : 1000u)) return 2;
        if (is_port) port = (unsigned)value;
        else cycles = (unsigned)value;
    }
    if ((argc - 3) % 2) return 2;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 1;
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(ROAD_JET_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    struct diag_link_cfg config = {argv[2], port, DIAG_AUTO, DIAG_LOOP, "roadjet", 1};
    struct diag_link link;
    diag_link_signals();
    diag_link_init(&link, &config);
    pid_t parent = getppid();
    unsigned count = 0;
    while ((!cycles || count < cycles) && diag_running && getppid() == parent && !diag_link_lost(&link)) {
        diag_link_tick(&link);
        struct road_jet_frame frame = {bd_mono_ms(), ROAD_JET_WAITING, 0, 0};
        if (diag_link_up(&link)) {
            frame = read_frame(&link);
            (void)diag_link_should_yield(&link, bd_mono_ms());
        }
        (void)sendto(fd, &frame, sizeof frame, 0,
                     (const struct sockaddr *)&address, sizeof address);
        if (cycles && ++count == cycles) break;
        poll(0, 0, frame.mode == ROAD_JET_WAITING || frame.mode == ROAD_JET_TOO_FAST ? 350 : 100);
    }
    int lost = diag_link_lost(&link);
    diag_link_close(&link);
    close(fd);
    return lost ? 1 : 0;
}
