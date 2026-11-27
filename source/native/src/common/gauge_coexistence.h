#ifndef ID6_GAUGE_COEXISTENCE_H
#define ID6_GAUGE_COEXISTENCE_H

#include <stddef.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Keep the existing app IPC separate from upstream-derived coordination. */
enum {
    GAUGE_BOOST_DATA_PORT=42860, GAUGE_SHIFT_DATA_PORT=42863,
    GAUGE_LAUNCH_DATA_PORT=42871, GAUGE_POWER_DATA_PORT=42872, GAUGE_BASE_DATA_PORT=42873,
    HUD_TELEMETRY_PORT=42970,
    BOOST_ACTIVE_PORT=42971, SHIFT_ACTIVE_PORT=42972, LAUNCH_ACTIVE_PORT=42973,
    GAUGE_MOST_PORT=42974,
    BOOST_MOST_PORT=42975, SHIFT_MOST_PORT=42976, LAUNCH_MOST_PORT=42977,
    WARM_ACTIVE_PORT=42978, WARM_MOST_PORT=42979,
    POWER_ACTIVE_PORT=42980, POWER_MOST_PORT=42981,
    BASE_ACTIVE_PORT=42982, BASE_MOST_PORT=42983
};
enum { HUD_SAMPLE_RPM=1, HUD_SAMPLE_BOOST=2, HUD_SAMPLE_N57=4 };
#define HUD_TELEMETRY_MAGIC UINT64_C(0x31504d534455484e)

struct hud_telemetry {
    uint64_t magic, rpm_ms, boost_ms;
    uint32_t source, valid, rpm;
    int32_t boost_tenths;
};
typedef char hud_telemetry_size_is_40[sizeof(struct hud_telemetry)==40 ? 1 : -1];

static inline unsigned gauge_source_port(const char *name) {
    if (!name) return 0;
    if (!strcmp(name,"boostdeck")) return BOOST_ACTIVE_PORT;
    if (!strcmp(name,"shiftdeck")) return SHIFT_ACTIVE_PORT;
    if (!strcmp(name,"launchdeck")) return LAUNCH_ACTIVE_PORT;
    if (!strcmp(name,"powerdeck")) return POWER_ACTIVE_PORT;
    if (!strcmp(name,"basediagnostik")) return BASE_ACTIVE_PORT;
    if (!strcmp(name,"warmdeck")) return WARM_ACTIVE_PORT;
    return 0;
}

static inline unsigned gauge_most_port(const char *name) {
    switch (gauge_source_port(name)) {
    case BOOST_ACTIVE_PORT: return BOOST_MOST_PORT;
    case SHIFT_ACTIVE_PORT: return SHIFT_MOST_PORT;
    case LAUNCH_ACTIVE_PORT: return LAUNCH_MOST_PORT;
    case POWER_ACTIVE_PORT: return POWER_MOST_PORT;
    case BASE_ACTIVE_PORT: return BASE_MOST_PORT;
    case WARM_ACTIVE_PORT: return WARM_MOST_PORT;
    default: return 0;
    }
}

static inline struct sockaddr_in gauge_local_address(unsigned port) {
    struct sockaddr_in a;
    memset(&a,0,sizeof a);
    a.sin_family=AF_INET; a.sin_port=htons((uint16_t)port);
    a.sin_addr.s_addr=htonl(0x7f000001u);
    return a;
}

/* These sockets are process-owned; abnormal exits also release the markers. */
static inline int gauge_bind(unsigned port) {
    struct sockaddr_in a=gauge_local_address(port);
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if (fd<0) return -1;
    if (fcntl(fd,F_SETFD,FD_CLOEXEC)==0 && fcntl(fd,F_SETFL,O_NONBLOCK)==0 &&
        bind(fd,(struct sockaddr *)&a,sizeof a)==0) return fd;
    int saved=errno;
    close(fd); errno=saved; return -1;
}

static inline int gauge_active(unsigned port) {
    int fd=gauge_bind(port);
    if (fd<0) return 1; /* Uncertain ownership must not start another poller. */
    close(fd); return 0;
}

static inline void gauge_publish(int fd,const struct hud_telemetry *sample) {
    struct sockaddr_in a=gauge_local_address(HUD_TELEMETRY_PORT);
    (void)sendto(fd,sample,sizeof *sample,0,(struct sockaddr *)&a,sizeof a);
}

static inline void gauge_request_most(void) {
    struct sockaddr_in a=gauge_local_address(GAUGE_MOST_PORT);
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if (fd>=0) {
        const char request=1;
        (void)sendto(fd,&request,1,0,(struct sockaddr *)&a,sizeof a);
        close(fd);
    }
}

static inline int native_android_auto_installed(void) {
#ifdef MOST_HOST
    const char *fake=getenv("ID6_FAKE_AAD_INSTALLED");
    return fake && !strcmp(fake,"1");
#elif defined(__QNX__)
    return access("/opt/aa/nbt-aad",F_OK)==0 ||
        access("/opt/hmi/data/ro/Plugins/NbtAad/NbtAad.manifest",F_OK)==0 ||
        access("/opt/hmi/ID5/data/ro/Plugins/NbtAad/NbtAad.manifest",F_OK)==0 ||
        access("/var/opt/hmi/data/ID5/Plugins/NbtAad/NbtAad.manifest",F_OK)==0;
#else
    return 0;
#endif
}

/* Projection installation is a veto for our renderer, never for engine diagnostics.
 * Missing shown files cannot establish HUD ownership; they may lag or fail to write. */
static inline int projection_hud_installed(void) {
    if (native_android_auto_installed()) return 1;
#ifdef MOST_HOST
    const char *cp=getenv("ID6_FAKE_CARPLAY_HUD"), *shown=getenv("ID6_FAKE_AAD_SHOWN");
    return (cp && !strcmp(cp,"1")) || (shown && !strcmp(shown,"1"));
#elif defined(__QNX__)
    return access("/var/opt/hmi/data/rw/Plugins/CarPlayMenu/librghook.so",F_OK)==0 ||
        access("/var/opt/hmi/data/ID5/Plugins/CarPlayMenu/librghook.so",F_OK)==0 ||
        access("/dev/shmem/nbt-aad-kombi-nav.shown",F_OK)==0 ||
        access("/dev/shmem/carplaymenu-kombi.shown",F_OK)==0 ||
        access("/dev/shmem/carplaymenu-most.shown",F_OK)==0;
#else
    return 0;
#endif
}
#endif
