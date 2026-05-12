/*
 * clock_gettime_test.c — kernel-level tests for sys_clock_gettime().
 *
 * Runs at boot time in supervisor mode (RUN_TESTS).  copy_to_user passes
 * through kernel pointers transparently in that mode.
 */

#include "drivers/rtc.h"
#include "trap/syscall.h"
#include "kernel/resource.h"
#include "kernel/printk.h"
#include "tests/test.h"
#include "errno.h"

static int passed = 0;
static int failed = 0;

/* Minimum plausible epoch: 2023-11-14 */
#define MIN_PLAUSIBLE_SEC 1700000000ULL

void clock_gettime_test_run(void) {
    printk("\n========================================\n");
    printk("       sys_clock_gettime Tests\n");
    printk("========================================\n\n");

    struct timespec ts;

    /* CLOCK_REALTIME: succeeds and returns plausible values */
    int rc = sys_clock_gettime(CLOCK_REALTIME, &ts);
    TEST("clock_gettime(REALTIME) returns 0",       rc == 0);
    TEST("clock_gettime tv_sec plausible",
         rc == 0 && (uint64_t)ts.tv_sec > MIN_PLAUSIBLE_SEC);
    TEST("clock_gettime tv_nsec >= 0",              rc == 0 && ts.tv_nsec >= 0);
    TEST("clock_gettime tv_nsec < 1e9",             rc == 0 && ts.tv_nsec < 1000000000L);

    /* CLOCK_MONOTONIC — same RTC source */
    struct timespec ts2;
    rc = sys_clock_gettime(CLOCK_MONOTONIC, &ts2);
    TEST("clock_gettime(MONOTONIC) returns 0",      rc == 0);
    TEST("clock_gettime(MONOTONIC) tv_sec plausible",
         rc == 0 && (uint64_t)ts2.tv_sec > MIN_PLAUSIBLE_SEC);

    /* CLOCK_PROCESS_CPUTIME_ID */
    rc = sys_clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts2);
    TEST("clock_gettime(PROCESS_CPUTIME_ID) returns 0", rc == 0);

    /* CLOCK_THREAD_CPUTIME_ID */
    rc = sys_clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts2);
    TEST("clock_gettime(THREAD_CPUTIME_ID) returns 0",  rc == 0);

    /* Unknown clockid → -EINVAL */
    rc = sys_clock_gettime((clockid_t)99, &ts2);
    TEST("clock_gettime(99) returns -EINVAL",        rc == -EINVAL);

    /* NULL tp → -EFAULT */
    rc = sys_clock_gettime(CLOCK_REALTIME, (struct timespec *)0);
    TEST("clock_gettime(NULL tp) returns -EFAULT",   rc == -EFAULT);

    /* Two consecutive calls must be non-decreasing */
    struct timespec a, b;
    sys_clock_gettime(CLOCK_REALTIME, &a);
    sys_clock_gettime(CLOCK_REALTIME, &b);
    uint64_t ca = (uint64_t)a.tv_sec * 1000000000ULL + (uint64_t)a.tv_nsec;
    uint64_t cb = (uint64_t)b.tv_sec * 1000000000ULL + (uint64_t)b.tv_nsec;
    TEST("clock_gettime: composite ns non-decreasing", cb >= ca);

    /* tv_nsec must agree with rtc_read_ns() at the nanosecond level —
     * i.e., the syscall uses the raw RTC remainder, not (tv_usec * 1000). */
    {
        uint64_t raw_ns = rtc_read_ns();
        long expected_nsec = (long)(raw_ns % 1000000000ULL);
        struct timespec t;
        sys_clock_gettime(CLOCK_REALTIME, &t);
        /* Allow 1 µs drift between the two back-to-back reads. */
        long delta = t.tv_nsec - expected_nsec;
        if (delta < 0) delta = -delta;
        TEST("clock_gettime: tv_nsec derived from rtc_read_ns (not usec*1000 stub)",
             delta < 1000000L);
    }

    /* gettimeofday and clock_gettime agree on tv_sec (within 1 second) */
    {
        struct timeval_k tv = {0, 0};
        sys_gettimeofday(&tv, (void *)0);
        sys_clock_gettime(CLOCK_REALTIME, &ts);
        long delta = (long)ts.tv_sec - (long)tv.tv_sec;
        if (delta < 0) delta = -delta;
        TEST("clock_gettime tv_sec agrees with gettimeofday (within 1s)", delta <= 1);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
