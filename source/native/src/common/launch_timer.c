#include "launch_timer.h"

#include <stdio.h>
#include <string.h>

void lt_set_mode(launch_timer *t, enum lt_mode mode) {
    static const unsigned limits[LT_MODE_COUNT][2] = {
        {0, LT_TARGET_MKPH}, {LT_TARGET_MKPH, 209215}, {0, 100000}, {100000, 200000}
    };
    memset(t, 0, sizeof *t);
    if ((unsigned)mode >= LT_MODE_COUNT) mode=LT_0_60;
    t->state = LT_IDLE;
    t->start_mkph=limits[mode][0];
    t->finish_mkph=limits[mode][1];
    t->last_kph=-1;
}

void lt_init(launch_timer *t) { lt_set_mode(t, LT_0_60); }

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
    if (!kph || now_ms - t->start_ms > LT_TIMEOUT_MS || kph <= t->peak_kph - LT_LIFT_KPH) return abort_run(t);
    if (kph > t->peak_kph) t->peak_kph = kph;
    if (kph == t->change_kph) return LT_EV_NONE;
    if (!t->start_mkph && !t->start_refined && kph > t->first_kph) refine_start(t, now_ms, kph);

    uint64_t split = crossing(t->change_ms, t->change_kph, now_ms, kph, LT_SPLIT_MKPH);
    uint64_t finish = crossing(t->change_ms, t->change_kph, now_ms, kph, (int32_t)t->finish_mkph);
    t->change_ms = now_ms;
    t->change_kph = kph;
    if (!t->start_mkph && split && !t->split30_ms) t->split30_ms = (uint32_t)(split - t->start_ms);
    if (!finish) return LT_EV_NONE;
    t->result_ms = (uint32_t)(finish - t->start_ms);
    t->state = LT_DONE;
    return LT_EV_FINISHED;
}

int lt_update(launch_timer *t, uint64_t now_ms, int kph) {
    uint64_t previous_ms=t->last_ms;
    int previous_kph=t->last_kph;
    if (previous_ms && now_ms<=previous_ms) return LT_EV_NONE;
    t->last_ms=now_ms;
    t->last_kph=kph;
    if (kph<0) {
        if (t->state==LT_RUNNING) return abort_run(t);
        if (t->state==LT_ARMED) t->state=LT_IDLE;
        return LT_EV_NONE;
    }
    if (t->state==LT_RUNNING) return running(t,now_ms,kph);
    if ((!t->start_mkph && !kph) ||
        (t->start_mkph && (unsigned)kph*1000<t->start_mkph)) {
        if (!t->start_mkph) t->last_zero_ms=now_ms;
        if (t->state==LT_ARMED) return LT_EV_NONE;
        t->state=LT_ARMED;
        return LT_EV_ARMED;
    }
    if (t->state!=LT_ARMED || previous_kph<0) return LT_EV_NONE;
    if (!t->start_mkph) {
        launch(t,now_ms,kph);
        if ((unsigned)kph*1000<t->finish_mkph) return LT_EV_STARTED;
        return abort_run(t); /* a start and finish in one quantized step is not measurable */
    }
    uint64_t start=crossing(previous_ms,previous_kph,now_ms,kph,(int32_t)t->start_mkph);
    if (!start) return LT_EV_NONE;
    launch(t,now_ms,kph);
    t->start_ms=start;
    t->change_ms=previous_ms;
    t->change_kph=previous_kph;
    int event=running(t,now_ms,kph);
    return event==LT_EV_NONE ? LT_EV_STARTED : event;
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
