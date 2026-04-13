/*
 * test_gettimeofday.c — us_test module for gettimeofday(2).
 *
 * Ported from the former standalone binary so that gettimeofday tests
 * are counted in the main SBUnix Userspace Test Suite total.
 */

#include <sys/time.h>
#include <unistd.h>
#include "us_test_common.h"

/* Minimum plausible Unix timestamp: 2023-11-14 (~1 700 000 000). */
#define MIN_PLAUSIBLE_SEC 1700000000L

static long gtod_to_usec(const struct timeval *tv) {
    return (long)tv->tv_sec * 1000000L + (long)tv->tv_usec;
}

void run_test_gettimeofday(us_test_group_t *group) {
    ut_group_begin(group, "gettimeofday");

    /* 1. Returns 0 */
    {
        struct timeval tv;
        TEST(group, "returns 0", gettimeofday(&tv, NULL) == 0);
    }

    /* 2. tv_sec is a plausible Unix timestamp (not the old stub zero) */
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        TEST(group, "tv_sec > 1700000000 (RTC live, not stub)", tv.tv_sec > MIN_PLAUSIBLE_SEC);
    }

    /* 3. tv_usec is in [0, 999 999] */
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        TEST(group, "tv_usec >= 0",      tv.tv_usec >= 0);
        TEST(group, "tv_usec < 1000000", tv.tv_usec < 1000000L);
    }

    /* 4. Two back-to-back calls are non-decreasing */
    {
        struct timeval t1, t2;
        gettimeofday(&t1, NULL);
        gettimeofday(&t2, NULL);
        TEST(group, "monotone: tv_sec non-decreasing",          t2.tv_sec >= t1.tv_sec);
        TEST(group, "monotone: composite usec non-decreasing",  gtod_to_usec(&t2) >= gtod_to_usec(&t1));
    }

    /* 5. After sleep(1), elapsed time is at least 1 second */
    {
        struct timeval before, after;
        gettimeofday(&before, NULL);
        sleep(1);
        gettimeofday(&after, NULL);
        long delta = (long)after.tv_sec - (long)before.tv_sec;
        TEST(group, "elapsed >= 1s after sleep(1)", delta >= 1);
        TEST(group, "elapsed <= 5s after sleep(1)", delta <= 5);
    }

    /* 6. NULL tv: no crash, returns 0 */
    {
        TEST(group, "NULL tv returns 0", gettimeofday(NULL, NULL) == 0);
    }

    /* 7. NULL tz: no crash, returns 0 */
    {
        struct timeval tv;
        TEST(group, "NULL tz returns 0", gettimeofday(&tv, NULL) == 0);
    }

    /* 8. Both NULL: returns 0 */
    {
        TEST(group, "both NULL returns 0", gettimeofday(NULL, NULL) == 0);
    }

    /* 9. Non-NULL tz is zeroed (UTC, no DST) */
    {
        struct timeval tv;
        struct timezone tz;
        tz.tz_minuteswest = 99;
        tz.tz_dsttime     = 99;
        gettimeofday(&tv, &tz);
        TEST(group, "tz.tz_minuteswest == 0", tz.tz_minuteswest == 0);
        TEST(group, "tz.tz_dsttime     == 0", tz.tz_dsttime     == 0);
    }

    /* 10. Composite usec is non-decreasing across 20 rapid calls */
    {
        struct timeval prev, cur;
        int monotone = 1;
        gettimeofday(&prev, NULL);
        for (int i = 0; i < 20; i++) {
            gettimeofday(&cur, NULL);
            if (gtod_to_usec(&cur) < gtod_to_usec(&prev)) { monotone = 0; break; }
            prev = cur;
        }
        TEST(group, "composite usec non-decreasing over 20 calls", monotone);
    }

    /* 11. tv_usec always < 1 000 000 across 20 rapid calls */
    {
        int valid = 1;
        for (int i = 0; i < 20; i++) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            if (tv.tv_usec < 0 || tv.tv_usec >= 1000000L) { valid = 0; break; }
        }
        TEST(group, "tv_usec always in [0,999999] over 20 calls", valid);
    }

    /* 12. tv_sec delta over sleep(1) is 1-5 s */
    {
        struct timeval t1, t2;
        gettimeofday(&t1, NULL);
        sleep(1);
        gettimeofday(&t2, NULL);
        long delta = (long)t2.tv_sec - (long)t1.tv_sec;
        TEST(group, "tv_sec delta after sleep(1) is 1-5s", delta >= 1 && delta <= 5);
    }

    ut_group_end(group);
}
