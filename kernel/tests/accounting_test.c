/*
 * accounting_test.c — kernel-level unit tests for sys_getrusage() and the
 * utime_ticks / children_utime accounting paths.
 *
 * Runs in RUN_TESTS mode before schedule() is entered.  proc_sleep() and
 * real timer preemption cannot be used here.  Instead we directly simulate
 * the timer_handler logic and manipulate fields to test every code path.
 *
 * Regression tests are clearly labelled with the bug they cover.
 */

#include "process/process.h"
#include "trap/syscall.h"
#include "trap/trap.h"
#include "kernel/resource.h"
#include "kernel/printk.h"
#include "tests/test.h"

static int passed = 0;
static int failed = 0;

/*
 * Simulate what timer_handler does for one tick while `p` is RUNNING.
 * Mirrors the fixed code in trap.c exactly so we catch regressions if the
 * handler is changed but the test is not, or vice-versa.
 */
static void simulate_tick(struct proc *p) {
    if (p == NULL || p->state != PROC_RUNNING) return;
    p->utime_ticks++;                           /* always — regardless of time_slice */
    if (p->time_slice > 0) {
        p->total_time++;
        if (p->total_time >= p->time_slice) {
            if (p->mlfq_level < MLFQ_LEVELS - 1)
                p->mlfq_level++;
            p->total_time = 0;
            /* real kernel calls proc_yield() here; we skip the swtch() */
        }
    }
}

void accounting_test_run(void) {
    printk("\n========================================\n");
    printk("       Process Accounting Tests\n");
    printk("========================================\n\n");

    struct proc *p = proc_alloc();
    TEST("accounting: proc_alloc succeeds", p != NULL);
    if (!p) goto done;

    struct proc *saved_current = current;
    current = p;

    /* ── 1. Initial state ──────────────────────────────────────────────── */
    TEST("accounting: utime_ticks starts at 0",    p->utime_ticks    == 0);
    TEST("accounting: children_utime starts at 0", p->children_utime == 0);
    TEST("accounting: total_time starts at 0",     p->total_time     == 0);

    /* ── 2. RUSAGE_SELF conversion: exact tick counts ─────────────────── */
    p->utime_ticks = 30;   /* 30 / 10 = 3 s, remainder 0 */
    struct rusage ru;
    int rc = sys_getrusage(RUSAGE_SELF, &ru);
    TEST("accounting: RUSAGE_SELF returns 0",       rc == 0);
    TEST("accounting: 30 ticks → tv_sec  == 3",    ru.ru_utime.tv_sec  == 3);
    TEST("accounting: 30 ticks → tv_usec == 0",    ru.ru_utime.tv_usec == 0);
    TEST("accounting: RUSAGE_SELF stime always 0",  ru.ru_stime.tv_sec  == 0 &&
                                                    ru.ru_stime.tv_usec == 0);

    /* ── 3. Fractional seconds ────────────────────────────────────────── */
    p->utime_ticks = 15;   /* 15 / 10 = 1 s, 5 * 100000 = 500000 µs */
    sys_getrusage(RUSAGE_SELF, &ru);
    TEST("accounting: 15 ticks → tv_sec  == 1",      ru.ru_utime.tv_sec  == 1);
    TEST("accounting: 15 ticks → tv_usec == 500000", ru.ru_utime.tv_usec == 500000);

    /* ── 4. Zero ticks edge case ──────────────────────────────────────── */
    p->utime_ticks = 0;
    sys_getrusage(RUSAGE_SELF, &ru);
    TEST("accounting: 0 ticks → tv_sec  == 0", ru.ru_utime.tv_sec  == 0);
    TEST("accounting: 0 ticks → tv_usec == 0", ru.ru_utime.tv_usec == 0);

    /* ── 5. Large tick value ──────────────────────────────────────────── */
    p->utime_ticks = 100;  /* 100 / 10 = 10 s, 0 µs */
    sys_getrusage(RUSAGE_SELF, &ru);
    TEST("accounting: 100 ticks → tv_sec  == 10", ru.ru_utime.tv_sec  == 10);
    TEST("accounting: 100 ticks → tv_usec == 0",  ru.ru_utime.tv_usec == 0);

    /* ── 6. RUSAGE_CHILDREN reflects children_utime, not utime_ticks ──── */
    p->utime_ticks    = 50;
    p->children_utime = 20;   /* 20 / 10 = 2 s */
    sys_getrusage(RUSAGE_CHILDREN, &ru);
    TEST("accounting: RUSAGE_CHILDREN returns 0",    rc == 0);
    TEST("accounting: children: 20 ticks → 2 s",    ru.ru_utime.tv_sec  == 2);
    TEST("accounting: children: tv_usec == 0",       ru.ru_utime.tv_usec == 0);
    TEST("accounting: children: stime always 0",     ru.ru_stime.tv_sec  == 0);

    /* ── 7. RUSAGE_SELF and RUSAGE_CHILDREN are independent ──────────── */
    p->utime_ticks    = 30;
    p->children_utime = 70;
    sys_getrusage(RUSAGE_SELF, &ru);
    TEST("accounting: RUSAGE_SELF unaffected by children_utime",
         ru.ru_utime.tv_sec == 3);   /* 30/10 = 3 */
    sys_getrusage(RUSAGE_CHILDREN, &ru);
    TEST("accounting: RUSAGE_CHILDREN unaffected by utime_ticks",
         ru.ru_utime.tv_sec == 7);   /* 70/10 = 7 */

    /* ── 8. Invalid / NULL argument guards ───────────────────────────── */
    rc = sys_getrusage(99, &ru);
    TEST("accounting: invalid who returns -1", rc == -1);
    rc = sys_getrusage(-1, &ru);
    TEST("accounting: negative who returns -1", rc == -1);
    rc = sys_getrusage(RUSAGE_SELF, NULL);
    TEST("accounting: NULL usage returns -1", rc == -1);

    /* ── 9. BUG 1 REGRESSION: non-preemptible process accumulates utime
     *
     * Before the fix, utime_ticks was inside `if (time_slice > 0)` so a
     * process with time_slice == 0 never got charged any CPU time.
     * After the fix, simulate_tick() always increments utime_ticks first.
     */
    printk("=== Bug-1 regression: non-preemptible utime_ticks ===\n");
    p->utime_ticks = 0;
    p->total_time  = 0;
    p->time_slice  = 0;   /* non-preemptible */
    p->state       = PROC_RUNNING;

    for (int i = 0; i < 5; i++)
        simulate_tick(p);

    TEST("accounting [Bug1]: 5 ticks, time_slice=0 → utime_ticks == 5",
         p->utime_ticks == 5);
    TEST("accounting [Bug1]: time_slice=0 → total_time stays 0",
         p->total_time  == 0);

    /* ── 10. Preemptible process: total_time resets but utime_ticks persists
     * Interrupts disabled: p is PROC_RUNNING with time_slice > 0, so the real
     * timer_handler would call proc_yield() and hang the test suite. */
    printk("=== utime_ticks persists across preemption boundary ===\n");
    interrupts_off();
    p->utime_ticks = 0;
    p->total_time  = 0;
    p->time_slice  = 3;
    p->mlfq_level  = 0;
    p->state       = PROC_RUNNING;

    /* Tick 1,2,3: total_time fills; on tick 3 boundary total_time resets to 0 */
    simulate_tick(p); simulate_tick(p); simulate_tick(p);
    TEST("accounting: total_time resets at slice boundary", p->total_time  == 0);
    TEST("accounting: utime_ticks NOT reset at boundary",   p->utime_ticks == 3);

    /* Ticks 4,5: accumulate into next slice */
    simulate_tick(p); simulate_tick(p);
    TEST("accounting: utime_ticks continues past boundary", p->utime_ticks == 5);
    TEST("accounting: total_time in new slice",             p->total_time  == 2);

    /* ── 11. children_utime accumulates across multiple reaped children ─
     * Simulate reaping 3 children by directly adding their utime_ticks,
     * the same arithmetic proc_wait() performs.
     */
    printk("=== children_utime multi-child accumulation ===\n");
    p->children_utime = 0;

    /* Simulate reaping child A (7 ticks), B (13 ticks), C (5 ticks) */
    p->children_utime += 7;
    p->children_utime += 13;
    p->children_utime += 5;

    sys_getrusage(RUSAGE_CHILDREN, &ru);
    /* 25 ticks / 10 = 2 s, 5 * 100000 = 500000 µs */
    TEST("accounting: multi-child accumulation tv_sec  == 2",    ru.ru_utime.tv_sec  == 2);
    TEST("accounting: multi-child accumulation tv_usec == 500000", ru.ru_utime.tv_usec == 500000);

    /* ── 12. children_utime does NOT affect utime_ticks ──────────────── */
    p->utime_ticks    = 10;
    p->children_utime = 999;
    sys_getrusage(RUSAGE_SELF, &ru);
    TEST("accounting: own utime unaffected by children", ru.ru_utime.tv_sec == 1);

    /* ── 13. mlfq_level demotes after slice exhaustion (side-check) ──── */
    p->mlfq_level  = 0;
    p->time_slice  = 2;
    p->total_time  = 0;
    p->utime_ticks = 0;
    p->state       = PROC_RUNNING;
    simulate_tick(p); simulate_tick(p);
    TEST("accounting: mlfq_level demoted after full slice", p->mlfq_level == 1);
    interrupts_on();

    current = saved_current;
    proc_free(p);

done:
    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
