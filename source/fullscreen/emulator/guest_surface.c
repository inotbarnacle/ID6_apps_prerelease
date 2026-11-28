/* IDEmu test transport. Production builds keep the QNX Screen renderer. */
#include "surface_protocol.h"
#include "emulator_telemetry.h"
#define ID6_EMULATOR 1
#include "boost_files.h"
#include "boost_config.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <sys/select.h>
#include <unistd.h>

static const char hex[] = "0123456789abcdef";
static const char *const preference_paths[] = {ID6_BOOST_PREFS, ID6_BOOST_LAYOUT, ID6_BOOST_DEFAULTS};

static void receive_preferences(const char *line)
{
    if (strlen(line)<10 || line[8]<'0' || line[8]>'2' || line[9]!=' ') return;
    const unsigned kind=(unsigned)(line[8]-'0');
    const size_t encoded=strlen(line+10);
    if (encoded%2 || encoded>50) return;
    const unsigned count=(unsigned)encoded/2;
    unsigned char data[25];
    for (unsigned i=0;i<count;++i) {
        const char *hi=strchr(hex,line[10+i*2]);
        const char *lo=strchr(hex,line[11+i*2]);
        if (!hi || !lo) return;
        data[i]=(unsigned char)((hi-hex)*16+lo-hex);
    }
    if (!boost_config_valid(kind,data,count)) return;
    const int fd=open(preference_paths[kind],O_WRONLY|O_CREAT|O_EXCL,0600);
    if (fd<0) return;
    if (write(fd,data,count)!=(ssize_t)count) unlink(preference_paths[kind]);
    close(fd);
}

static void send_preferences(int port)
{
    static unsigned char previous[3][25];
    static unsigned previous_sizes[3];
    for (unsigned kind=0;kind<3;++kind) {
        unsigned char data[26];
        const int fd=open(preference_paths[kind],O_RDONLY);
        if (fd<0) continue;
        const ssize_t got=read(fd,data,sizeof data);
        close(fd);
        if (got<=0 || !boost_config_valid(kind,data,(size_t)got)) continue;
        const unsigned size=(unsigned)got;
        if (size==previous_sizes[kind] && !memcmp(data,previous[kind],size)) continue;
        char line[64];
        memcpy(line,"ID6CONF 0 ",10);
        line[8]=(char)('0'+kind);
        for (unsigned i=0;i<size;++i) {
            line[10+i*2]=hex[data[i]>>4];
            line[11+i*2]=hex[data[i]&15];
        }
        line[10+2*size]='\n';
        if (write(port,line,11+2*size)==(ssize_t)(11+2*size)) {
            memcpy(previous[kind],data,size);
            previous_sizes[kind]=size;
        }
    }
}

static void telemetry_line(const char *line)
{
    struct emulator_telemetry sample;
    struct timespec time;
    char extra;
    if (sscanf(line, "ID6CAN %d %d %d %d %d %d %d %c", &sample.rpm, &sample.kph,
               &sample.coolant, &sample.pedal_tenths, &sample.gear,
               &sample.steering_hundredths, &sample.ignition, &extra) != 7) return;
    if (sample.rpm < 0 || sample.rpm > 8000 || sample.kph < 0 || sample.kph > 300 ||
        sample.coolant < -40 || sample.coolant > 130 || sample.pedal_tenths < 0 ||
        sample.pedal_tenths > 1000 || sample.gear < 0 || sample.gear > 3 ||
        sample.steering_hundredths < -72000 || sample.steering_hundredths > 72000 ||
        sample.ignition < 0 || sample.ignition > 85) return;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) return;
    sample.sampled_ms = (uint64_t)time.tv_sec * 1000u + time.tv_nsec / 1000000u;
    const int fd = open(IDEMU_TELEMETRY_FILE, O_WRONLY | O_CREAT, 0600);
    if (fd < 0) return;
    (void)write(fd, &sample, sizeof sample);
    close(fd);
}

static int open_serial(void)
{
    const int port = open("/dev/ser5", O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (port < 0) return -1;
    struct termios settings;
    if (tcgetattr(port, &settings) == 0) {
        settings.c_lflag &= ~(ICANON | ECHO);
        settings.c_oflag &= ~OPOST;
        settings.c_cc[VMIN] = 0;
        settings.c_cc[VTIME] = 0;
        (void)tcsetattr(port, TCSANOW, &settings);
    }
    return port;
}

int main(void)
{
    int port = -1;
    char line[128];
    unsigned used = 0;
    struct surface_state state;
    char packet[8 + sizeof(state) * 2];
    const char prefix[] = "ID6HUD ";
    for (unsigned i=0; i<sizeof(prefix)-1; ++i) packet[i] = prefix[i];
    packet[sizeof(packet)-1] = '\n';
    while (getppid() > 1) {
        fd_set ready;
        struct timeval timeout = {1,0};
        FD_ZERO(&ready);
        FD_SET(STDIN_FILENO,&ready);
        if (port >= 0) FD_SET(port,&ready);
        int available = select(port >= 0 ? port+1 : STDIN_FILENO+1,&ready,0,0,&timeout);
        if (available < 0 && errno == EINTR) continue;
        if (available <= 0) break;
        if (port >= 0 && FD_ISSET(port,&ready)) {
            char bytes[256];
            ssize_t count = read(port,bytes,sizeof bytes);
            for (ssize_t i=0; i<count; ++i) {
                if (bytes[i] == '\n') {
                    if (used < sizeof line) {
                        line[used]=0;
                        if (!strncmp(line,"ID6CONF ",8)) receive_preferences(line);
                        else telemetry_line(line);
                    }
                    used = 0;
                } else if (used < sizeof line) line[used++] = bytes[i];
            }
        }
        if (!FD_ISSET(STDIN_FILENO,&ready)) continue;
        if (read(STDIN_FILENO,&state,sizeof state) != (ssize_t)sizeof state) break;
        if (port < 0 && state.page == surface_hidden) continue;
        if (port < 0 && (port = open_serial()) < 0) break;
        const unsigned char *bytes = (const unsigned char *)&state;
        for (unsigned i=0; i<sizeof(state); ++i) {
            packet[7+i*2] = hex[bytes[i]>>4];
            packet[8+i*2] = hex[bytes[i]&15];
        }
        ssize_t n = write(port,packet,sizeof packet);
        if (n < 0 && errno != EAGAIN && errno != EINTR) break;
        if (state.page == surface_hidden) {
            close(port);
            port = -1;
            used = 0;
        } else if (state.page == surface_boost) {
            send_preferences(port);
        }
    }
    if (port >= 0) close(port);
    return 0;
}
