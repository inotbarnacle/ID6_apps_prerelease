#ifndef LAUNCH_TIMER_H
#define LAUNCH_TIMER_H

/* Acceleration timer driven by the DME's integer km/h (OBD PID 0x0D) samples.
   Pure logic: no I/O, no clocks. The caller passes monotonic milliseconds. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    LT_TIMEOUT_MS = 60000,    /* a run slower than this is abandoned */
    LT_LIFT_KPH = 4,          /* falling this far below the run's peak aborts */
    LT_SPLIT_MKPH = 48280,    /* 30 mph in thousandths of km/h */
    LT_TARGET_MKPH = 96561    /* 60 mph in thousandths of km/h */
};

enum lt_mode { LT_0_60, LT_60_130, LT_0_100, LT_100_200, LT_MODE_COUNT };
enum lt_state { LT_IDLE, LT_ARMED, LT_RUNNING, LT_DONE, LT_ABORTED };
enum lt_event { LT_EV_NONE, LT_EV_ARMED, LT_EV_STARTED, LT_EV_FINISHED, LT_EV_ABORTED };

typedef struct {
    enum lt_state state;
    uint64_t last_ms;       /* newest accepted sample */
    unsigned start_mkph, finish_mkph;
    int last_kph;
    uint64_t last_zero_ms;  /* newest sample at 0 km/h */
    uint64_t start_ms;      /* estimated launch instant */
    uint64_t change_ms;     /* newest sample whose speed differed from the one before */
    int change_kph;
    uint64_t first_ms;      /* first non-zero speed of the run */
    int first_kph;
    int start_refined;
    int peak_kph;
    uint32_t split30_ms;    /* 0 until reached */
    uint32_t result_ms;     /* 0 until reached */
} launch_timer;

void lt_init(launch_timer *t);
void lt_set_mode(launch_timer *t, enum lt_mode mode);
/* kph < 0 means the sample is unavailable. Returns an lt_event. */
int lt_update(launch_timer *t, uint64_t now_ms, int kph);
/* Live elapsed time while running, the result when done, else 0. */
uint32_t lt_elapsed(const launch_timer *t, uint64_t now_ms);
/* Whether `candidate` beats the stored `best` (0 means no record). */
int lt_is_better(uint32_t candidate, uint32_t best);
/* "s.hh", truncated to hundredths, capped at the timeout. */
void lt_format_seconds(uint32_t ms, char *out, size_t len);

#ifdef __cplusplus
}
#endif
#endif
