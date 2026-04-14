/*
 * test_time.c — us_test module for time(), clock_gettime(), and nanosleep().
 *
 * Verifies that these libc wrappers reach the kernel RTC rather than
 * returning the old hardcoded-zero stubs.
 */

#include <time.h>
#include <unistd.h>
#include "us_test_common.h"

/* Minimum plausible Unix timestamp: 2023-11-14 (~1 700 000 000). */
#define MIN_PLAUSIBLE_SEC 1700000000L

void run_test_time(us_test_group_t *group) {
    ut_group_begin(group, "time/clock_gettime/nanosleep");

    /* 1. time(NULL) returns a plausible epoch value */
    {
        time_t t = time(NULL);
        TEST(group, "time(NULL) > 1700000000 (not stub zero)", t > (time_t)MIN_PLAUSIBLE_SEC);
    }

    /* 2. time() writes the same value to its argument */
    {
        time_t out = 0;
        time_t ret = time(&out);
        TEST(group, "time(&out): ret == out", ret == out);
        TEST(group, "time(&out): out > 1700000000", out > (time_t)MIN_PLAUSIBLE_SEC);
    }

    /* 3. Two consecutive time() calls are non-decreasing */
    {
        time_t t1 = time(NULL);
        time_t t2 = time(NULL);
        TEST(group, "time() non-decreasing", t2 >= t1);
    }

    /* 4. clock_gettime(CLOCK_REALTIME) returns a plausible tv_sec */
    {
        struct timespec ts;
        int rc = clock_gettime(0 /* CLOCK_REALTIME */, &ts);
        TEST(group, "clock_gettime returns 0", rc == 0);
        TEST(group, "clock_gettime tv_sec > 1700000000", ts.tv_sec > (time_t)MIN_PLAUSIBLE_SEC);
    }

    /* 5. clock_gettime tv_nsec is in [0, 999999999] */
    {
        struct timespec ts;
        clock_gettime(0, &ts);
        TEST(group, "clock_gettime tv_nsec >= 0",         ts.tv_nsec >= 0);
        TEST(group, "clock_gettime tv_nsec < 1000000000", ts.tv_nsec < 1000000000L);
    }

    /* 6. clock_gettime and time() agree on tv_sec (within 1 second) */
    {
        time_t t = time(NULL);
        struct timespec ts;
        clock_gettime(0, &ts);
        long delta = (long)ts.tv_sec - (long)t;
        if (delta < 0) delta = -delta;
        TEST(group, "clock_gettime tv_sec matches time() within 1s", delta <= 1);
    }

    /* 7. clock_gettime(CLOCK_MONOTONIC) also returns plausible value */
    {
        struct timespec ts;
        int rc = clock_gettime(1 /* CLOCK_MONOTONIC */, &ts);
        TEST(group, "clock_gettime(MONOTONIC) returns 0", rc == 0);
        TEST(group, "clock_gettime(MONOTONIC) tv_sec > 1700000000", ts.tv_sec > (time_t)MIN_PLAUSIBLE_SEC);
    }

    /* 8. clock_gettime(NULL tp) returns -1 */
    {
        int rc = clock_gettime(0, NULL);
        TEST(group, "clock_gettime(NULL tp) returns -1", rc == -1);
    }

    /* 9. nanosleep(NULL) returns -1 */
    {
        int rc = nanosleep(NULL, NULL);
        TEST(group, "nanosleep(NULL req) returns -1", rc == -1);
    }

    /* 10. nanosleep for 1 second actually delays by ~1 second */
    {
        struct timespec req = { 1, 0 };
        time_t before = time(NULL);
        nanosleep(&req, NULL);
        time_t after = time(NULL);
        long delta = (long)after - (long)before;
        TEST(group, "nanosleep(1s) elapsed >= 1s", delta >= 1);
        TEST(group, "nanosleep(1s) elapsed <= 5s", delta <= 5);
    }

    /* 11. nanosleep with zero seconds returns immediately (no hang) */
    {
        struct timespec req = { 0, 0 };
        int rc = nanosleep(&req, NULL);
        TEST(group, "nanosleep(0,0) returns 0", rc == 0);
    }

    /* 12. nanosleep clears rem on normal return */
    {
        struct timespec req = { 0, 0 };
        struct timespec rem = { 99, 99 };
        nanosleep(&req, &rem);
        TEST(group, "nanosleep clears rem.tv_sec",  rem.tv_sec  == 0);
        TEST(group, "nanosleep clears rem.tv_nsec", rem.tv_nsec == 0);
    }

    ut_group_end(group);
}
