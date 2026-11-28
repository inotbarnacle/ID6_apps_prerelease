#ifndef DIAGNOSTIC_WORKER_H
#define DIAGNOSTIC_WORKER_H

#include "diagnostic_lease.h"
#include "paths.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <spawn.h>
#include <string.h>
#include <unistd.h>

// Read the diagnostic gateway's address that setup.sh saved on the data partition: one IPv4 line.
static inline bool read_gateway(const char* path, char (&host)[16]) {
    FILE* file = fopen(path, "r");
    if (!file) return false;
    char line[32];
    bool valid = fgets(line, sizeof line, file) != NULL && fgetc(file) == EOF;
    fclose(file);
    if (!valid) return false;
    line[strcspn(line, "\r\n")] = 0;
    struct in_addr address;
    if (inet_pton(AF_INET, line, &address) != 1 || !gateway_address_ok(ntohl(address.s_addr))) return false;
    strcpy(host, line);
    return true;
}

static inline bool installed_gateway(char (&host)[16]) { return read_gateway(ENA_GATEWAY_FILE, host); }

// Attributes for a worker started from inside HmiMain3D. HmiMain3D's threads block signals, and a child inherits the
// mask, so without an empty mask the SIGTERM that stops a worker would never arrive. The default set leaves out
// SIGKILL and SIGSTOP: the car's glibc 2.15 posix_spawn calls sigaction() for every signal in it and exits 127 in the
// child when one is refused (seen on the car by AA-EntryNav). Its own process group keeps it out of HmiMain3D's.
static inline int worker_spawn_attr(posix_spawnattr_t* attr) {
    int error = posix_spawnattr_init(attr);
    if (error) return error;
    sigset_t none, defaults;
    sigemptyset(&none);
    sigemptyset(&defaults);
    for (int s = 1; s < 32; ++s)
        if (s != SIGKILL && s != SIGSTOP) sigaddset(&defaults, s);
    error = posix_spawnattr_setsigmask(attr, &none);
    if (!error) error = posix_spawnattr_setsigdefault(attr, &defaults);
    if (!error) error = posix_spawnattr_setpgroup(attr, 0);
    if (!error)
        error = posix_spawnattr_setflags(attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    if (error) posix_spawnattr_destroy(attr);
    return error;
}

// Start a worker with stdin and stderr on /dev/null and stdout on output (or /dev/null). -1 if it could not start.
static inline pid_t launch(const char *const *args,int output) {
    pid_t child=-1;
    char library_path[] = "LD_LIBRARY_PATH=/lib:/usr/lib";
    char *environment[] = {library_path, 0};
    posix_spawnattr_t attr;
    int error = worker_spawn_attr(&attr);
    if (error) return -1;
    posix_spawn_file_actions_t actions;
    error = posix_spawn_file_actions_init(&actions);
    if (!error) {
        error = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
        if (!error) {
            if (output < 0) error=posix_spawn_file_actions_addopen(&actions,1,"/dev/null",O_WRONLY,0);
            else error=posix_spawn_file_actions_adddup2(&actions,output,1);
        }
        if (!error) error = posix_spawn_file_actions_addopen(&actions,2,"/dev/null",O_WRONLY,0);
        if (!error)
            error = posix_spawn(&child, args[0], &actions, &attr,
                                const_cast<char *const *>(args), environment);
        posix_spawn_file_actions_destroy(&actions);
    }
    posix_spawnattr_destroy(&attr);
    return error ? -1 : child;
}

#endif
