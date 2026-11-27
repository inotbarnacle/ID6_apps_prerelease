#include "launch_timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

/* Feed a constant speed every step_ms from `from` to `to` (inclusive). Returns the last event. */
static int hold(launch_timer *t, uint64_t from, uint64_t to, unsigned step, int kph) {
    int last = LT_EV_NONE;
    for (uint64_t now = from; now <= to; now += step) {
        int ev = lt_update(t, now, kph);
        if (ev != LT_EV_NONE) last = ev;
    }
    return last;
}

/* Constant acceleration from 0, sampled every 65 ms, integer km/h as the DME reports it. */
static int run_constant_accel(launch_timer *t, uint64_t launch_ms, double kph_per_s, uint64_t until) {
    int finished = 0;
    for (uint64_t now = launch_ms; now <= until; now += 65) {
        double seconds = (double)(now - launch_ms) / 1000.0;
        int kph = (int)(kph_per_s * seconds);
        if (lt_update(t, now, kph) == LT_EV_FINISHED) finished = 1;
    }
    return finished;
}

static void test_arms_on_first_standstill(void) {
    launch_timer t;
    lt_init(&t);
    CHECK(t.state == LT_IDLE);
    CHECK(lt_update(&t, 1000, 0) == LT_EV_ARMED);
    CHECK(t.state == LT_ARMED);
}

static void test_moving_car_does_not_arm(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 3000, 50, 40);
    CHECK(t.state == LT_IDLE);
}

static void test_measures_zero_to_sixty(void) {
    /* 96.5606 km/h in 5.0 s => 19.31212 km/h per second. */
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 1000, 50, 0);
    CHECK(t.state == LT_ARMED);
    CHECK(run_constant_accel(&t, 1050, 19.31212, 9000));
    CHECK(t.state == LT_DONE);
    CHECK(abs((int)t.result_ms - 5000) <= 90);
    CHECK(abs((int)t.split30_ms - 2500) <= 90);
}

static void test_slow_run_within_tolerance(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 1000, 50, 0);
    CHECK(run_constant_accel(&t, 1050, 96.5606 / 7.4, 15000));
    CHECK(abs((int)t.result_ms - 7400) <= 90);
}

static void test_reports_started_once(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 1000, 50, 0);
    CHECK(lt_update(&t, 1100, 2) == LT_EV_STARTED);
    CHECK(t.state == LT_RUNNING);
    CHECK(lt_update(&t, 1165, 4) == LT_EV_NONE);
}

static void test_lifting_aborts(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 1000, 50, 0);
    lt_update(&t, 1100, 5);
    lt_update(&t, 1500, 30);
    CHECK(lt_update(&t, 1600, 30 - LT_LIFT_KPH) == LT_EV_ABORTED);
    CHECK(t.state == LT_ABORTED);
    CHECK(t.result_ms == 0);
}

static void test_stopping_mid_run_rearms(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 1000, 50, 0);
    lt_update(&t, 1100, 3);
    CHECK(lt_update(&t, 1200, 0) == LT_EV_ABORTED);
    hold(&t, 1250, 1250 + 500, 50, 0);
    CHECK(t.state == LT_ARMED);
}

static void test_lost_data_aborts(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 1000, 50, 0);
    lt_update(&t, 1100, 10);
    CHECK(lt_update(&t, 1200, -1) == LT_EV_ABORTED);
}

static void test_timeout_aborts(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 1000, 50, 0);
    lt_update(&t, 1100, 5);
    CHECK(hold(&t, 1150, 1100 + LT_TIMEOUT_MS + 100, 100, 50) == LT_EV_ABORTED);
}

static void test_invalid_speed_never_arms(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 2000, 50, -1);
    CHECK(t.state == LT_IDLE);
}

static void test_done_rearms_after_stop_and_keeps_result(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 0, 1000, 50, 0);
    run_constant_accel(&t, 1050, 19.31212, 9000);
    uint32_t result = t.result_ms;
    hold(&t, 9100, 9100 + 500, 50, 0);
    CHECK(t.state == LT_ARMED);
    CHECK(t.result_ms == result);
    CHECK(lt_update(&t, 9800, 3) == LT_EV_STARTED);
    CHECK(t.result_ms == 0);
}

static void test_elapsed_live_and_frozen(void) {
    launch_timer t;
    lt_init(&t);
    CHECK(lt_elapsed(&t, 5000) == 0);
    hold(&t, 0, 1000, 50, 0);
    lt_update(&t, 1100, 2);
    CHECK(lt_elapsed(&t, 2100) >= 1000 && lt_elapsed(&t, 2100) <= 1100);
    run_constant_accel(&t, 1050, 19.31212, 9000);
    CHECK(lt_elapsed(&t, 60000) == t.result_ms);
}

static void test_clock_going_backwards_is_ignored(void) {
    launch_timer t;
    lt_init(&t);
    hold(&t, 1000, 2000, 50, 0);
    lt_update(&t, 2100, 4);
    CHECK(lt_update(&t, 500, 50) == LT_EV_NONE);
    CHECK(t.state == LT_RUNNING);
}

static void test_best_record(void) {
    CHECK(lt_is_better(5200, 0));
    CHECK(lt_is_better(5200, 5300));
    CHECK(!lt_is_better(5300, 5200));
    CHECK(!lt_is_better(0, 5200));
}

static void test_format_seconds(void) {
    char text[16];
    lt_format_seconds(5034, text, sizeof text);
    CHECK(!strcmp(text, "5.03"));
    lt_format_seconds(12995, text, sizeof text);
    CHECK(!strcmp(text, "12.99"));
    lt_format_seconds(0, text, sizeof text);
    CHECK(!strcmp(text, "0.00"));
    lt_format_seconds(LT_TIMEOUT_MS + 5000, text, sizeof text);
    CHECK(!strcmp(text, "60.00"));
}

static void test_rolling_interpolates_both_thresholds(void) {
    launch_timer timer;
    lt_set_mode(&timer, LT_100_200);
    CHECK(lt_update(&timer, 1000, 98) == LT_EV_ARMED);
    CHECK(lt_update(&timer, 1200, 102) == LT_EV_STARTED);
    CHECK(timer.start_ms == 1100);
    CHECK(lt_update(&timer, 1400, -1) == LT_EV_ABORTED);
    CHECK(lt_update(&timer, 2000, 98) == LT_EV_ARMED);
    CHECK(lt_update(&timer, 2200, 102) == LT_EV_STARTED);
    CHECK(lt_update(&timer, 15000, 198) == LT_EV_NONE);
    CHECK(lt_update(&timer, 15200, 202) == LT_EV_FINISHED);
    CHECK(timer.result_ms == 13000);
}

int main(void) {
    { /* A positive fractional N57 speed must never arm a standing start. */
      launch_timer t;
      lt_init(&t);
      CHECK(lt_update_mkph(&t,1000,5)==LT_EV_NONE && t.state==LT_IDLE);
      CHECK(lt_update_mkph(&t,1100,0)==LT_EV_ARMED);
      CHECK(lt_update_mkph(&t,1200,5)==LT_EV_STARTED);
      CHECK(lt_update_mkph(&t,1300,300001)==LT_EV_ABORTED);
      lt_set_mode(&t,LT_100_200);
      CHECK(lt_update_mkph(&t,1000,99900)==LT_EV_ARMED);
      CHECK(lt_update_mkph(&t,1100,100100)==LT_EV_STARTED && t.start_ms==1050);
      CHECK(lt_update_mkph(&t,2100,199900)==LT_EV_NONE);
      CHECK(lt_update_mkph(&t,2200,200100)==LT_EV_FINISHED && t.result_ms==1100);
    }
    test_rolling_interpolates_both_thresholds();
    test_arms_on_first_standstill();
    test_moving_car_does_not_arm();
    test_measures_zero_to_sixty();
    test_slow_run_within_tolerance();
    test_reports_started_once();
    test_lifting_aborts();
    test_stopping_mid_run_rearms();
    test_lost_data_aborts();
    test_timeout_aborts();
    test_invalid_speed_never_arms();
    test_done_rearms_after_stop_and_keeps_result();
    test_elapsed_live_and_frozen();
    test_clock_going_backwards_is_ignored();
    test_best_record();
    test_format_seconds();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("launch timer: all tests passed");
    return 0;
}
