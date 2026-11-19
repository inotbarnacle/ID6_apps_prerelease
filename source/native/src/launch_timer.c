#include "launch_timer.h"

#include <stdio.h>
#include <string.h>

void lt_init(launch_timer *t) {
    memset(t, 0, sizeof *t);
    t->state = LT_IDLE;
}

static int abort_run(launch_timer *t) {
    t->state = LT_ABORTED;
    t->split30_ms = 0;
    t->result_ms = 0;
    return LT_EV_ABORTED;
}

/* Instant the speed crossed `target_mkph` between two reported values, or 0 if it did not. */
static uint64_t crossing(uint64_t from_ms, int from_kph, uint64_t to_ms, int to_kph, int32_t target_mkph) {
    int32_t from = from_kph * 1000, to = to_kph * 1000;
    if (!(from < target_mkph && target_mkph <= to)) return 0;
    return from_ms + (uint64_t)(target_mkph - from) * (to_ms - from_ms) / (uint64_t)(to - from);
}

/* Extrapolate the first two speed steps back to 0 km/h, bounded by the last standstill sample. */
static void refine_start(launch_timer *t, uint64_t now_ms, int kph) {
    uint64_t span = now_ms - t->first_ms;
    uint64_t back = (uint64_t)t->first_kph * span / (uint64_t)(kph - t->first_kph);
    uint64_t estimate = back > t->first_ms ? 0 : t->first_ms - back;
    if (estimate < t->last_zero_ms) estimate = t->last_zero_ms;
    t->start_ms = estimate;
    t->start_refined = 1;
}

static int standstill(launch_timer *t, uint64_t now_ms) {
    int aborted = t->state == LT_RUNNING;
    t->last_zero_ms = now_ms;
    if (aborted) {
        t->zero_since_ms = now_ms;
        return abort_run(t);
    }
    if (!t->zero_since_ms) t->zero_since_ms = now_ms;
    if (t->state != LT_ARMED && now_ms - t->zero_since_ms >= LT_ARM_MS) {
        t->state = LT_ARMED;
        return LT_EV_ARMED;
    }
    return LT_EV_NONE;
}

static int launch(launch_timer *t, uint64_t now_ms, int kph) {
    t->state = LT_RUNNING;
    t->split30_ms = 0;
    t->result_ms = 0;
    t->start_ms = now_ms;
    t->start_refined = 0;
    t->first_ms = t->change_ms = now_ms;
    t->first_kph = t->change_kph = t->peak_kph = kph;
    return LT_EV_STARTED;
}

static int running(launch_timer *t, uint64_t now_ms, int kph) {
    if (now_ms - t->start_ms > LT_TIMEOUT_MS || kph <= t->peak_kph - LT_LIFT_KPH) return abort_run(t);
    if (kph > t->peak_kph) t->peak_kph = kph;
    if (kph == t->change_kph) return LT_EV_NONE;
    if (!t->start_refined && kph > t->first_kph) refine_start(t, now_ms, kph);

    uint64_t split = crossing(t->change_ms, t->change_kph, now_ms, kph, LT_SPLIT_MKPH);
    uint64_t finish = crossing(t->change_ms, t->change_kph, now_ms, kph, LT_TARGET_MKPH);
    t->change_ms = now_ms;
    t->change_kph = kph;
    if (split && !t->split30_ms) t->split30_ms = (uint32_t)(split - t->start_ms);
    if (!finish) return LT_EV_NONE;
    t->result_ms = (uint32_t)(finish - t->start_ms);
    t->state = LT_DONE;
    return LT_EV_FINISHED;
}

int lt_update(launch_timer *t, uint64_t now_ms, int kph) {
    if (t->last_ms && now_ms < t->last_ms) return LT_EV_NONE;
    t->last_ms = now_ms;
    if (kph < 0) {
        t->zero_since_ms = 0;
        return t->state == LT_RUNNING ? abort_run(t) : LT_EV_NONE;
    }
    if (kph == 0) return standstill(t, now_ms);
    t->zero_since_ms = 0;
    if (t->state == LT_ARMED) return launch(t, now_ms, kph);
    if (t->state == LT_RUNNING) return running(t, now_ms, kph);
    return LT_EV_NONE;
}

uint32_t lt_elapsed(const launch_timer *t, uint64_t now_ms) {
    if (t->state == LT_DONE) return t->result_ms;
    if (t->state != LT_RUNNING || now_ms <= t->start_ms) return 0;
    uint64_t elapsed = now_ms - t->start_ms;
    return elapsed > LT_TIMEOUT_MS ? LT_TIMEOUT_MS : (uint32_t)elapsed;
}

int lt_is_better(uint32_t candidate, uint32_t best) {
    return candidate > 0 && (best == 0 || candidate < best);
}

void lt_format_seconds(uint32_t ms, char *out, size_t len) {
    if (ms > LT_TIMEOUT_MS) ms = LT_TIMEOUT_MS;
    snprintf(out, len, "%u.%02u", ms / 1000u, (ms % 1000u) / 10u);
}
