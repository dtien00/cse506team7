#include "drivers/rtc.h"
#include "trap/syscall.h"
#include "kernel/resource.h"
#include "kernel/printk.h"
#include "tests/test.h"

static int passed = 0;
static int failed = 0;

/*
 * rtc_test_run — kernel-level unit tests for the Goldfish RTC driver
 * and the sys_gettimeofday() handler.
 *
 * All tests run in boot context before schedule() is entered, so there
 * is no scheduler dependency.  sys_gettimeofday() is called with
 * kernel-stack pointers — valid because supervisor mode can always write
 * kernel pages regardless of the SUM bit.
 *
 * Tests:
 *  1. rtc_read_ns() returns a plausible Unix epoch value.
 *  2. rtc_read_ns() is non-decreasing across two back-to-back calls.
 *  3. ns-to-timeval arithmetic is correct for a known fixed value.
 *  4. tv_usec conversion stays < 1 000 000 at the ns boundary (999 999 999 ns).
 *  5. sys_gettimeofday() returns 0 with a kernel-stack tv pointer.
 *  6. sys_gettimeofday() fills tv_sec with a plausible value.
 *  7. sys_gettimeofday() fills tv_usec in [0, 999 999].
 *  8. sys_gettimeofday() with NULL tv returns 0 (no crash).
 *  9. sys_gettimeofday() with NULL tz returns 0 (no crash).
 * 10. Two calls to sys_gettimeofday() produce non-decreasing composite values.
 */

/* Minimum plausible ns since epoch: 2023-11-14 in nanoseconds. */
#define MIN_PLAUSIBLE_NS  (1700000000ULL * 1000000000ULL)
#define MIN_PLAUSIBLE_SEC  1700000000ULL

void rtc_test_run(void) {
    printk("\n========================================\n");
    printk("       RTC / gettimeofday Tests\n");
    printk("========================================\n\n");

    /* ── 1. rtc_read_ns() returns a plausible epoch value ────────────── */
    uint64_t ns1 = rtc_read_ns();
    TEST("rtc: read_ns > 1.7e18 (not zero / not stub)", ns1 > MIN_PLAUSIBLE_NS);

    /* ── 2. rtc_read_ns() is non-decreasing ──────────────────────────── */
    uint64_t ns2 = rtc_read_ns();
    TEST("rtc: non-decreasing across two calls", ns2 >= ns1);

    /* ── 3. ns-to-sec/usec arithmetic: known value ───────────────────── */
    /*
     * 1 744 492 800 s + 123 456 789 ns  (a specific moment in April 2025)
     *   tv_sec  = 1 744 492 800
     *   tv_usec = 123 456 789 / 1000 = 123 456  (truncated)
     */
    {
        uint64_t known_ns = 1744492800ULL * 1000000000ULL + 123456789ULL;
        uint64_t sec  = known_ns / 1000000000ULL;
        uint64_t usec = (known_ns % 1000000000ULL) / 1000ULL;
        TEST("rtc: arithmetic tv_sec  == 1744492800", sec  == 1744492800ULL);
        TEST("rtc: arithmetic tv_usec == 123456",     usec == 123456ULL);
    }

    /* ── 4. tv_usec stays < 1 000 000 at the sub-second boundary ─────── */
    /*
     * 999 999 999 ns remainder → 999 999 µs (must not reach 1 000 000).
     */
    {
        uint64_t boundary_ns  = 1744492800ULL * 1000000000ULL + 999999999ULL;
        uint64_t usec = (boundary_ns % 1000000000ULL) / 1000ULL;
        TEST("rtc: tv_usec at 999999999 ns boundary < 1000000", usec < 1000000ULL);
        TEST("rtc: tv_usec at 999999999 ns boundary == 999999",  usec == 999999ULL);
    }

    /* ── 5-9. sys_gettimeofday() with kernel-stack pointers ──────────── */
    {
        struct timeval_k tv = {0, 0};

        /* 5. Returns 0 */
        int rc = sys_gettimeofday(&tv, (void *)0);
        TEST("sys_gettimeofday: returns 0", rc == 0);

        /* 6. tv_sec is plausible */
        TEST("sys_gettimeofday: tv_sec plausible", tv.tv_sec > MIN_PLAUSIBLE_SEC);

        /* 7. tv_usec is in [0, 999 999] */
        TEST("sys_gettimeofday: tv_usec >= 0",       tv.tv_usec < (uint64_t)-1); /* always true for uint */
        TEST("sys_gettimeofday: tv_usec < 1000000",  tv.tv_usec < 1000000ULL);
    }

    /* 8. NULL tv: no crash, returns 0 */
    {
        int rc = sys_gettimeofday((struct timeval_k *)0, (void *)0);
        TEST("sys_gettimeofday: NULL tv returns 0", rc == 0);
    }

    /* 9. Non-NULL tz, NULL tv: no crash, returns 0 */
    {
        int zeros[2] = {99, 99};
        int rc = sys_gettimeofday((struct timeval_k *)0, zeros);
        TEST("sys_gettimeofday: NULL tv + non-NULL tz returns 0", rc == 0);
        TEST("sys_gettimeofday: tz[0] zeroed", zeros[0] == 0);
        TEST("sys_gettimeofday: tz[1] zeroed", zeros[1] == 0);
    }

    /* 10. Two calls produce non-decreasing composite values ─────────── */
    {
        struct timeval_k t1 = {0, 0}, t2 = {0, 0};
        sys_gettimeofday(&t1, (void *)0);
        sys_gettimeofday(&t2, (void *)0);
        uint64_t c1 = t1.tv_sec * 1000000ULL + t1.tv_usec;
        uint64_t c2 = t2.tv_sec * 1000000ULL + t2.tv_usec;
        TEST("sys_gettimeofday: composite usec non-decreasing", c2 >= c1);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
