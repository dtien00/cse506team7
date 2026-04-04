#include "trap/trap.h"
#include "trap/syscall.h"
#include "process/process.h"
#include "kernel/printk.h"
#include "tests/test.h"

static int passed = 0;
static int failed = 0;

/*
 * timer_test_run — kernel-level unit tests for sys_uptime and the wake_tick
 * wakeup mechanism that backs sys_sleep.
 *
 * Constraints: these tests run before schedule() is entered (RUN_TESTS mode).
 * proc_sleep() cannot be called here because it calls swtch() to the scheduler
 * which has never run.  Instead we test the wake_tick scan logic directly by
 * manipulating timer_ticks and calling the same loop that timer_handler uses.
 */

/* Run the same wakeup scan that timer_handler performs after each tick. */
static void run_wakeup_scan(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        struct proc *p = &proctab[i];
        if (p->state == PROC_BLOCKED && p->wake_tick != 0
                && timer_ticks >= p->wake_tick) {
            p->wake_tick = 0;
            proc_wakeup(p);   /* p slept on itself as the channel */
        }
    }
}

void timer_test_run(void) {
    printk("\n========================================\n");
    printk("       Timer / Sleep Tests\n");
    printk("========================================\n\n");

    /* ── 1. timer_ticks is positive after boot ─────────────────────────────
     * The timer fires at ~10 Hz during kernel init; by the time tests run
     * at least a few ticks have elapsed.
     */
    TEST("uptime: timer_ticks > 0 after boot", timer_ticks > 0);

    /* ── 2. sys_uptime() reflects timer_ticks ──────────────────────────────
     * There may be a tick between the two reads, so accept either value.
     */
    uint64_t snap = timer_ticks;
    int reported = sys_uptime();
    TEST("uptime: sys_uptime() matches timer_ticks",
         (uint64_t)reported == snap || (uint64_t)reported == snap + 1);

    /* ── 3. sys_uptime() is non-decreasing across two calls ────────────────*/
    int t1 = sys_uptime();
    int t2 = sys_uptime();
    TEST("uptime: non-decreasing across two calls", t2 >= t1);

    /* ── 4. wake_tick fires on deadline ────────────────────────────────────
     * Allocate a process, put it in BLOCKED state via proc_block (which adds
     * it to blocked_queue so proc_wakeup can find and remove it), set a
     * wake_tick, advance timer_ticks past the deadline, run the scan, and
     * confirm the process is now READY with wake_tick cleared.
     */
    printk("=== wake_tick wakeup mechanism ===\n");
    struct proc *p = proc_alloc();
    TEST("sleep: proc_alloc succeeds", p != NULL);
    if (p) {
        proc_block(p);                          /* → BLOCKED, in blocked_queue */
        p->chan = p;                            /* mirror what proc_sleep(p) sets */
        TEST("sleep: proc_block sets BLOCKED", p->state == PROC_BLOCKED);

        uint64_t saved_ticks = timer_ticks;
        p->wake_tick = saved_ticks + 5;         /* deadline 5 ticks away */

        /* Deadline not yet reached — scan should leave the process alone. */
        run_wakeup_scan();
        TEST("sleep: proc stays BLOCKED before deadline",
             p->state == PROC_BLOCKED && p->wake_tick != 0);

        /* Advance past the deadline and run the scan again. */
        timer_ticks = p->wake_tick;
        run_wakeup_scan();

        TEST("sleep: proc moves to READY after deadline", p->state == PROC_READY);
        TEST("sleep: wake_tick cleared after wakeup", p->wake_tick == 0);

        /* Restore ticks; drain p from ready_queue so it doesn't pollute later
         * tests.  In RUN_TESTS mode schedule() is never called, so leaving the
         * proc in the queue would also be harmless, but this is cleaner. */
        timer_ticks = saved_ticks;
        struct proc *drain;
        do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    /* ── 5. wake_tick = 0 never triggers wakeup ────────────────────────────
     * A process blocked for reasons other than sleep has wake_tick == 0.
     * Even when timer_ticks is huge it must not be woken by the scan.
     */
    printk("=== wake_tick=0 guard ===\n");
    struct proc *p2 = proc_alloc();
    TEST("sleep: proc_alloc for guard test", p2 != NULL);
    if (p2) {
        proc_block(p2);
        p2->wake_tick = 0;                      /* not sleeping on a timer */

        uint64_t saved2 = timer_ticks;
        timer_ticks = 0xFFFFFFFFFFFFFFFFULL;    /* max possible tick value */
        run_wakeup_scan();
        timer_ticks = saved2;

        TEST("sleep: wake_tick=0 never wakes proc", p2->state == PROC_BLOCKED);

        /* Clean up: move to READY then free. */
        proc_unblock(p2);
        struct proc *d2;
        do { d2 = proc_next_ready(); } while (d2 && d2 != p2);
        proc_free(p2);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
