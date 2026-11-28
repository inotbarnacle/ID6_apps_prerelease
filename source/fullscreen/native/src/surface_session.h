#ifndef ID6_SURFACE_SESSION_H
#define ID6_SURFACE_SESSION_H

#include "surface_protocol.h"
#include "gauge_core.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

class SurfaceSession {
    int sender_;
    int reader_;
    pid_t process_;
    surface_state lastState_;
    uint64_t lastSent_;
    bool hasState_;
    uint64_t nextOpen_;

public:
    SurfaceSession() : sender_(-1), reader_(-1), process_(-1), lastSent_(0), hasState_(false), nextOpen_(0) {}
    ~SurfaceSession() { closeChannel(); }

    void reap() {
        if (process_ <= 0) return;
        const pid_t result = waitpid(process_, 0, WNOHANG);
        if (result == process_ || (result < 0 && errno == ECHILD)) {
            process_ = -1;
            closeChannel();
        }
    }
    void closeChannel() {
        hasState_ = false;
        if (sender_ >= 0) { close(sender_); sender_ = -1; }
        if (reader_ >= 0) { close(reader_); reader_ = -1; }
    }
    bool sendState(const surface_state& state, uint64_t now) {
        if (sender_ < 0) return false;
        if (hasState_ && !memcmp(&lastState_, &state, sizeof state) &&
            (state.page == surface_hidden || now - lastSent_ < 200))
            return true;

        // Keep the read end until waitpid observes the renderer exit, avoiding SIGPIPE in HmiMain3D.
        const ssize_t count = write(sender_, &state, sizeof state);
        if (count == static_cast<ssize_t>(sizeof state)) {
            lastState_ = state;
            lastSent_ = now;
            hasState_ = true;
            return true;
        }
        if (count < 0 && (errno == EAGAIN || errno == EINTR)) return true;
        closeChannel();
        return false;
    }

    bool open(const char* root) {
        reap();
        if (sender_ >= 0) return true;
        if (process_ > 0) return false;
        const uint64_t now=bd_mono_ms();
        if (now<nextOpen_) return false;
        // An incompatible or failing renderer must not become a rapid spawn loop.
        nextOpen_=now+2000;
        char path[180], settings[180], width[12], height[12];
        snprintf(path, sizeof path, "%s/id6-library-fullscreen-qnx", root);
        snprintf(settings, sizeof settings, "%s/screen-size.txt", root);
        unsigned w = 1280, h = 480;
        FILE* file = fopen(settings, "r");
        if (file) {
            char extra;
            if (fscanf(file, "%u %u %c", &w, &h, &extra) != 2) w = h = 0;
            fclose(file);
        }
        snprintf(width, sizeof width, "%u", w);
        snprintf(height, sizeof height, "%u", h);
        int fd[2];
        if (pipe(fd) < 0) return false;
        bool configured = fcntl(fd[1],F_SETFL,O_NONBLOCK) == 0 &&
                          fcntl(fd[1],F_SETFD,FD_CLOEXEC) == 0;
#ifdef __QNX__
        if (configured) {
            const int null = ::open("/dev/null", O_RDWR);
            if (null >= 0) {
                int descriptors[] = {fd[0], null, null};
                struct inheritance inheritance;
                memset(&inheritance, 0, sizeof inheritance);
                inheritance.flags = SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
                sigfillset(&inheritance.sigdefault);
                sigemptyset(&inheritance.sigmask);
                char libraries[] = "LD_LIBRARY_PATH=/opt/hmi/lib:/opt/sys/lib:/lib:/usr/lib:/boot/lib";
                char* environment[] = {libraries, 0};
                char* argv[] = {path, width, height, 0};
                process_ = spawn(path, 3, descriptors, &inheritance, argv, environment);
                close(null);
            }
        }
#else
        (void)configured;
#endif
        if (process_ < 0) { close(fd[0]); close(fd[1]); return false; }
        reader_ = fd[0];
        fcntl(reader_,F_SETFD,FD_CLOEXEC);
        sender_ = fd[1];
        return true;
    }
};

#endif
