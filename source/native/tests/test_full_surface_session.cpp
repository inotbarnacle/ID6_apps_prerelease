#include "gauge_core.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static uint64_t clock_ms=1000;
static unsigned pipe_attempts=0;
static uint64_t test_clock() { return clock_ms; }
static int counted_pipe(int *fds) { ++pipe_attempts;return ::pipe(fds); }
#define bd_mono_ms test_clock
#define pipe counted_pipe
#include "surface_session.h"
#undef pipe
#undef bd_mono_ms

int main() {
    SurfaceSession failed;
    // Host preflight creates and closes the real pipe; no QNX spawn is available.
    assert(!failed.open("/nonexistent-host-renderer") && pipe_attempts==1);
    for (unsigned i=0;i<100;++i) assert(!failed.open("/nonexistent-host-renderer"));
    assert(pipe_attempts==1);
    clock_ms=2999;assert(!failed.open("/nonexistent-host-renderer") && pipe_attempts==1);
    clock_ms=3000;assert(!failed.open("/nonexistent-host-renderer") && pipe_attempts==2);
    assert(failed.sender_<0 && failed.reader_<0 && failed.process_<0);

    SurfaceSession active;
    int channel[2];assert(::pipe(channel)==0);
    active.reader_=channel[0];active.sender_=channel[1];
    pid_t child=fork();assert(child>=0);
    if (!child) {
        close(channel[1]);surface_state data;
        ssize_t n=read(channel[0],&data,sizeof data);
        _exit(n==sizeof data && surface_packet_valid(&data,sizeof data) ? 0 : 1);
    }
    active.process_=child;
    active.nextOpen_=clock_ms+2000;
    assert(active.open("/nonexistent-host-renderer") && pipe_attempts==2);
    surface_state packet;memset(&packet,0,sizeof packet);packet.magic=surface_magic;packet.page=surface_warm;
    assert(active.sendState(packet,clock_ms));
    int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    active.reap();
    assert(active.sender_<0 && active.reader_<0 && active.process_<0);
    assert(fcntl(channel[0],F_GETFD)<0 && errno==EBADF);
    assert(fcntl(channel[1],F_GETFD)<0 && errno==EBADF);
    assert(!active.open("/nonexistent-host-renderer") && pipe_attempts==2);
    puts("Surface session: bounded failed restarts, active channel writes, child exit and pipe release passed.");
}
