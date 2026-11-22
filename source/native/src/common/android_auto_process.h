#ifndef ID6_ANDROID_AUTO_PROCESS_H
#define ID6_ANDROID_AUTO_PROCESS_H

#include <stdio.h>
#include <string.h>

enum { AA_PROCESS_UNKNOWN = -1, AA_PROCESS_STOPPED = 0, AA_PROCESS_RUNNING = 1 };

/* Read pidin's PID/Arguments columns, never a substring in another process's
 * arguments. Match the executable basename so NBTApps packaging also works.
 * An incomplete listing must not enable a second MOST client. */
static inline int android_auto_process_listing(FILE *stream) {
    char line[1024], executable[512];
    int seen = 0, uncertain = 0, running = 0;
    if (!stream) return AA_PROCESS_UNKNOWN;
    while (fgets(line, sizeof line, stream)) {
        long pid = 0;
        int fields = sscanf(line, "%ld %511s", &pid, executable);
        if (fields >= 1 && pid > 0) {
            ++seen;
            if (fields != 2 || strlen(executable) == sizeof executable - 1) uncertain = 1;
            else {
                const char *base = strrchr(executable, '/');
                base = base ? base + 1 : executable;
                if (!strcmp(base, "nbt-aad")) running = 1;
            }
        }
        /* Do not interpret a long argument continuation as another process. */
        if (!strchr(line, '\n')) {
            int c;
            while ((c = fgetc(stream)) != '\n' && c != EOF) {}
        }
    }
    if (running) return AA_PROCESS_RUNNING;
    return ferror(stream) || !seen || uncertain ? AA_PROCESS_UNKNOWN : AA_PROCESS_STOPPED;
}

static inline int android_auto_process_blocks_most(int state, int command_status) {
    return command_status != 0 || state != AA_PROCESS_STOPPED;
}

static inline int native_android_auto_running(void) {
#ifdef __QNX__
    /* Once at worker startup, outside the HMI dispatcher. A missing or failed
     * pidin keeps HSFZ-only behavior; no state-file or PID reuse guesses. */
    FILE *stream = popen("/usr/bin/pidin ar 2>/dev/null", "r");
    if (!stream) return 1;
    int state = android_auto_process_listing(stream);
    int status = pclose(stream);
    return android_auto_process_blocks_most(state, status);
#else
    return 0;
#endif
}

#endif
