/*
 * test_nanosleep.c — Tests for nanosleep() and clock_nanosleep() (Section 6)
 *
 * Covers:
 *   T1  Basic duration     — nanosleep(1 s) elapses ~10 ticks
 *   T2  Zero duration      — nanosleep({0,0}) returns 0 immediately
 *   T3  Sub-second         — nanosleep(100 ms = 1 tick) elapses 1-2 ticks
 *   T4  NULL remainder     — nanosleep with rem=NULL does not crash
 *   T5  usleep()           — usleep(200000) (~2 ticks) elapses correctly
 *   T6  clock_nanosleep    — CLOCK_MONOTONIC relative, same timing as T1
 *   T7  CLOCK_REALTIME     — must return error (not implemented)
 *   T8  TIMER_ABSTIME past — target in the past returns immediately
 *   T9  TIMER_ABSTIME fut  — target 1 s in the future elapses ~10 ticks
 *
 * Timer resolution: TICKS_PER_SEC = 10 (0.1 s per tick).
 * Allowed window: ±20% plus 1 tick for scheduler jitter.
 */

#include <time.h>
#include <unistd.h>
#include "us_test_common.h"

#define NS_TICKS_PER_SEC 10   /* matches kernel TICKS_PER_SEC */

void run_test_nanosleep(us_test_group_t *group) {
    ut_group_begin(group, "nanosleep");

    /* ── T1: Basic 1-second sleep ────────────────────────────────────────────
     * nanosleep({1, 0}) must block for ~10 ticks.
     * Allowed range: 8–14 ticks (accounts for MLFQ jitter).
     */
    {
        struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };
        int before = uptime();
        int r = nanosleep(&req, (struct timespec *)0);
        int elapsed = uptime() - before;

        TEST(group, "nanosleep/1s: returns 0", r == 0);
        TEST(group, "nanosleep/1s: elapsed 8-14 ticks",
             elapsed >= 8 && elapsed <= 14);
    }

    /* ── T2: Zero-duration sleep returns immediately ─────────────────────────
     * nanosleep({0, 0}) must return 0 without blocking.
     */
    {
        struct timespec req = { .tv_sec = 0, .tv_nsec = 0 };
        int before = uptime();
        int r = nanosleep(&req, (struct timespec *)0);
        int elapsed = uptime() - before;

        TEST(group, "nanosleep/zero: returns 0", r == 0);
        TEST(group, "nanosleep/zero: elapsed <= 1 tick", elapsed <= 1);
    }

    /* ── T3: Sub-second sleep (100 ms = 1 tick) ──────────────────────────────
     * nanosleep({0, 100_000_000}) = 100 ms = exactly 1 tick at 10 Hz.
     * Expect 1-3 ticks (rounding up plus one tick of scheduler overhead).
     */
    {
        struct timespec req = { .tv_sec = 0, .tv_nsec = 100000000L };
        int before = uptime();
        int r = nanosleep(&req, (struct timespec *)0);
        int elapsed = uptime() - before;

        TEST(group, "nanosleep/100ms: returns 0", r == 0);
        TEST(group, "nanosleep/100ms: elapsed 1-3 ticks",
             elapsed >= 1 && elapsed <= 3);
    }

    /* ── T4: rem=NULL does not crash ─────────────────────────────────────────
     * Calling nanosleep with rem=NULL must complete without faulting.
     */
    {
        struct timespec req = { .tv_sec = 0, .tv_nsec = 100000000L };
        int r = nanosleep(&req, (struct timespec *)0);
        TEST(group, "nanosleep/null-rem: returns 0 without crash", r == 0);
    }

    /* ── T5: usleep() via nanosleep ──────────────────────────────────────────
     * usleep(200000) = 200 ms = 2 ticks.  Expect 2-4 ticks.
     * Previously usleep() was a no-op stub; now it calls nanosleep().
     */
    {
        int before = uptime();
        usleep(200000);
        int elapsed = uptime() - before;

        TEST(group, "usleep/200ms: elapsed 2-4 ticks",
             elapsed >= 2 && elapsed <= 4);
    }

    /* ── T6: clock_nanosleep CLOCK_MONOTONIC relative ───────────────────────
     * clock_nanosleep(CLOCK_MONOTONIC, 0, {1,0}, NULL) ≈ same as nanosleep.
     * Expect 8–14 ticks.
     */
    {
        struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };
        int before = uptime();
        int r = clock_nanosleep(CLOCK_MONOTONIC, 0, &req, (struct timespec *)0);
        int elapsed = uptime() - before;

        TEST(group, "clock_nanosleep/relative: returns 0", r == 0);
        TEST(group, "clock_nanosleep/relative: elapsed 8-14 ticks",
             elapsed >= 8 && elapsed <= 14);
    }

    /* ── T7: clock_nanosleep CLOCK_REALTIME returns error ────────────────────
     * CLOCK_REALTIME is not implemented (no RTC driver).
     * The call must return -1 (with errno set, or just -1 raw).
     */
    {
        struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };
        int r = clock_nanosleep(CLOCK_REALTIME, 0, &req, (struct timespec *)0);

        TEST(group, "clock_nanosleep/CLOCK_REALTIME: returns error (-1)",
             r == -1);
    }

    /* ── T8: clock_nanosleep TIMER_ABSTIME in the past returns immediately ───
     * Pass abs_time = 1 (far in the past relative to any reasonable uptime).
     * Must return 0 without blocking (elapsed <= 1 tick).
     */
    {
        struct timespec past = { .tv_sec = 1, .tv_nsec = 0 };
        int before = uptime();
        int r = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                &past, (struct timespec *)0);
        int elapsed = uptime() - before;

        TEST(group, "clock_nanosleep/abstime-past: returns 0", r == 0);
        TEST(group, "clock_nanosleep/abstime-past: returns immediately (<=1 tick)",
             elapsed <= 1);
    }

    /* ── T9: clock_nanosleep TIMER_ABSTIME 1 s in the future ─────────────────
     * Compute abs = uptime_ticks_as_timespec + 1 s, then sleep until then.
     * Expect ~10 ticks of elapsed time (8–14 ticks allowed).
     *
     * uptime() returns ticks; convert to seconds for the timespec.
     * Since TICKS_PER_SEC = 10, abs.tv_sec = uptime/10 + 1.
     */
    {
        int now_ticks = uptime();
        /* Convert current tick count to a timespec (monotonic origin = boot). */
        struct timespec abs_target;
        abs_target.tv_sec  = (long)(now_ticks / NS_TICKS_PER_SEC) + 1;
        abs_target.tv_nsec = (long)((now_ticks % NS_TICKS_PER_SEC) *
                                    (1000000000L / NS_TICKS_PER_SEC));

        int before = uptime();
        int r = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                &abs_target, (struct timespec *)0);
        int elapsed = uptime() - before;

        TEST(group, "clock_nanosleep/abstime-future: returns 0", r == 0);
        TEST(group, "clock_nanosleep/abstime-future: elapsed 8-14 ticks",
             elapsed >= 8 && elapsed <= 14);
    }

    ut_group_end(group);
}
