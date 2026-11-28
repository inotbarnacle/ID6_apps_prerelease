// The workers' spawn attributes (src/diagnostic_worker.h) with the car's own glibc 2.15, under QEMU. A file action runs
// in the child only after glibc has reset the signals in the "default" set, so a marker file the child creates proves it
// got past that step. Attributes that reset every signal (sigfillset) must stop there with exit 127, as on the car.
//   test-spawn --hu-glibc   both: sigfillset stops the child, worker_spawn_attr lets it on
//   test-spawn              worker_spawn_attr lets the child on (any glibc)
#include "diagnostic_worker.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

extern char** environ;

static bool spawn_reaches_marker(bool fill_all, const char* marker, int* status) {
    unlink(marker);
    posix_spawnattr_t attr;
    if (fill_all) {
        sigset_t none, all;
        posix_spawnattr_init(&attr);
        sigemptyset(&none);
        sigfillset(&all);
        posix_spawnattr_setsigmask(&attr, &none);
        posix_spawnattr_setsigdefault(&attr, &all);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    } else if (worker_spawn_attr(&attr) != 0) {
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 9, marker, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    char program[] = "/bin/true";
    char* args[] = {program, NULL};
    pid_t pid = -1;
    int error = posix_spawn(&pid, program, &actions, &attr, args, environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    *status = -1;
    if (!error)
        while (waitpid(pid, status, 0) < 0 && errno == EINTR) {
        }
    struct stat info;
    bool reached = stat(marker, &info) == 0;
    unlink(marker);
    return reached;
}

int main(int argc, char** argv) {
    bool hu = argc > 1 && !strcmp(argv[1], "--hu-glibc");
    char marker[64];
    snprintf(marker, sizeof marker, "/tmp/entrynav-apps-spawn-%d", (int)getpid());
    // Like HmiMain3D: SIGTERM blocked and SIGPIPE ignored in the parent.
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGTERM);
    sigprocmask(SIG_BLOCK, &blocked, NULL);
    signal(SIGPIPE, SIG_IGN);
    int fill_status = 0, worker_status = 0;
    bool fill_reached = spawn_reaches_marker(true, marker, &fill_status);
    bool worker_reached = spawn_reaches_marker(false, marker, &worker_status);
    bool ok = worker_reached &&
              (!hu || (!fill_reached && WIFEXITED(fill_status) && WEXITSTATUS(fill_status) == 127));
    printf("%s worker spawn: worker_spawn_attr child %s; sigfillset child %s (status %d)\n", ok ? "PASS" : "FAIL",
           worker_reached ? "went on" : "STOPPED", fill_reached ? "went on" : "stopped", fill_status);
    return ok ? 0 : 1;
}
