/*
 * test_gettimeofday — standalone test suite for gettimeofday(2).
 *
 * Runs as its own binary (/bin/test_gettimeofday) so it appears as a
 * separate suite in both `make test` and `make qemu` output.
 *
 * Exit code: 0 = all passed, 1 = at least one failure.
 */

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

/* ── Minimal inline test framework ───────────────────────────────────── */

static int g_passed = 0;
static int g_failed = 0;

static void check(int cond, const char *name) {
    if (cond) {
        g_passed++;
    } else {
        g_failed++;
        printf("  [FAIL] %s\n", name);
    }
}

#define TEST(name, cond) check((cond), (name))

/* ── Helpers ─────────────────────────────────────────────────────────── */

/* Minimum plausible Unix timestamp: 2023-11-14 (~1 700 000 000). */
#define MIN_PLAUSIBLE_SEC 1700000000L

static long tv_to_usec(const struct timeval *tv) {
    return (long)tv->tv_sec * 1000000L + (long)tv->tv_usec;
}

/* ── Test cases ──────────────────────────────────────────────────────── */

int main(void) {
    printf("\n========================================\n");
    printf("  gettimeofday test suite\n");
    printf("========================================\n");

    /* 1. Returns 0 */
    {
        struct timeval tv;
        TEST("returns 0", gettimeofday(&tv, NULL) == 0);
    }

    /* 2. tv_sec is a plausible Unix timestamp (not the old stub zero) */
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        TEST("tv_sec > 1700000000 (RTC live, not stub)", tv.tv_sec > MIN_PLAUSIBLE_SEC);
    }

    /* 3. tv_usec is in [0, 999 999] — microseconds, not nanoseconds */
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        TEST("tv_usec >= 0",      tv.tv_usec >= 0);
        TEST("tv_usec < 1000000", tv.tv_usec < 1000000L);
    }

    /* 4. Two back-to-back calls are non-decreasing */
    {
        struct timeval t1, t2;
        gettimeofday(&t1, NULL);
        gettimeofday(&t2, NULL);
        TEST("monotone: tv_sec non-decreasing",       t2.tv_sec >= t1.tv_sec);
        TEST("monotone: composite usec non-decreasing", tv_to_usec(&t2) >= tv_to_usec(&t1));
    }

    /* 5. After sleep(1), elapsed time is at least 1 second */
    {
        struct timeval before, after;
        gettimeofday(&before, NULL);
        sleep(1);
        gettimeofday(&after, NULL);
        long delta = (long)after.tv_sec - (long)before.tv_sec;
        TEST("elapsed >= 1s after sleep(1)", delta >= 1);
        TEST("elapsed <= 5s after sleep(1)", delta <= 5);
    }

    /* 6. NULL tv: no crash, returns 0 */
    {
        TEST("NULL tv returns 0", gettimeofday(NULL, NULL) == 0);
    }

    /* 7. NULL tz: no crash, returns 0 */
    {
        struct timeval tv;
        TEST("NULL tz returns 0", gettimeofday(&tv, NULL) == 0);
    }

    /* 8. Both NULL: returns 0 */
    {
        TEST("both NULL returns 0", gettimeofday(NULL, NULL) == 0);
    }

    /* 9. Non-NULL tz is zeroed (UTC, no DST) */
    {
        struct timeval tv;
        struct timezone tz;
        tz.tz_minuteswest = 99;   /* poison to detect non-write */
        tz.tz_dsttime     = 99;
        gettimeofday(&tv, &tz);
        TEST("tz.tz_minuteswest == 0", tz.tz_minuteswest == 0);
        TEST("tz.tz_dsttime     == 0", tz.tz_dsttime     == 0);
    }

    /* 10. Composite usec is non-decreasing across 20 rapid calls */
    {
        struct timeval prev, cur;
        int monotone = 1;
        gettimeofday(&prev, NULL);
        for (int i = 0; i < 20; i++) {
            gettimeofday(&cur, NULL);
            if (tv_to_usec(&cur) < tv_to_usec(&prev)) { monotone = 0; break; }
            prev = cur;
        }
        TEST("composite usec non-decreasing over 20 calls", monotone);
    }

    /* 11. tv_usec always < 1 000 000 across 20 rapid calls */
    {
        int valid = 1;
        for (int i = 0; i < 20; i++) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            if (tv.tv_usec < 0 || tv.tv_usec >= 1000000L) { valid = 0; break; }
        }
        TEST("tv_usec always in [0,999999] over 20 calls", valid);
    }

    /* 12. tv_sec delta over sleep(1) is 1–5 s */
    {
        struct timeval t1, t2;
        gettimeofday(&t1, NULL);
        sleep(1);
        gettimeofday(&t2, NULL);
        long delta = (long)t2.tv_sec - (long)t1.tv_sec;
        TEST("tv_sec delta after sleep(1) is 1-5s", delta >= 1 && delta <= 5);
    }

    /* ── Summary ─────────────────────────────────────────────────────── */
    printf("----------------------------------------\n");
    printf("TOTAL: %d passed, %d failed\n", g_passed, g_failed);
    printf("========================================\n\n");

    return g_failed == 0 ? 0 : 1;
}
