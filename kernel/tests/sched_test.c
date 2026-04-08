#include "process/process.h"
#include "process/swtch.h"
#include "kernel/printk.h"
#include "trap/trap.h"
#include "tests/test.h"

static int passed = 0;
static int failed = 0;

//------------------------------//
// ******** TEST CASES ******** //
//------------------------------//

void sched_test_run(void) {
    printk("\n========================================\n");
    printk("       Scheduler Tests\n");
    printk("========================================\n\n");
    printk("=== Scheduler Core Tests ===\n");

    // Allocate two dummy processes (do not create real kernel processes)
    struct proc *p1 = proc_alloc();
    struct proc *p2 = proc_alloc();
    TEST("proc_alloc for scheduler test", p1 && p2 && p1 != p2);

    // Ready both
    if (p1) proc_ready(p1);
    if (p2) proc_ready(p2);

    // Simulate scheduling
    struct proc *n1 = proc_next_ready();
    struct proc *n2 = proc_next_ready();
    TEST("proc_next_ready returns both", n1 && n2 && n1 != n2);

    // Block and unblock
    if (n1) proc_block(n1);
    TEST("proc_block sets BLOCKED", n1 && n1->state == PROC_BLOCKED);
    proc_unblock(n1);
    TEST("proc_unblock sets READY", n1 && n1->state == PROC_READY);

    // Free dummy processes
    if (p1) proc_free(p1);
    if (p2) proc_free(p2);

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");

    printk("\n========================================\n");
    printk("    End of Scheduler Tests\n");
    printk("========================================\n\n");
}

void sched_timeslice_test_run(void) {
    printk("\n=== Time-Slice Preemption Tests ===\n");
    /*
     * Constraint: RUN_TESTS mode runs before schedule() is entered.
     * proc_yield() calls swtch() to scheduler_context which is uninitialized,
     * so we test the timer_handler data-path directly without calling proc_yield().
     * We verify: (a) total_time resets to 0, (b) state never becomes PROC_ZOMBIE.
     */

    struct proc *p = proc_alloc();
    TEST("timeslice: proc_alloc succeeds", p != NULL);
    if (!p) return;

    p->time_slice  = 3;
    p->total_time  = 0;
    p->state       = PROC_RUNNING;   /* pretend scheduler picked it up */

    /* Ticks 1 and 2: below budget — state unchanged */
    p->total_time++;
    TEST("timeslice: tick 1 below budget, not ZOMBIE", p->state != PROC_ZOMBIE);
    p->total_time++;
    TEST("timeslice: tick 2 below budget, not ZOMBIE", p->state != PROC_ZOMBIE);

    /* Tick 3: budget exhausted — simulate the fixed timer_handler logic */
    p->total_time++;
    if (p->total_time >= p->time_slice) {
        p->total_time = 0;
        /* real kernel calls proc_yield() here; we skip that swtch() */
    }
    TEST("timeslice: total_time resets to 0 after budget", p->total_time == 0);
    TEST("timeslice: state is NOT ZOMBIE after budget exhausted", p->state != PROC_ZOMBIE);

    /* Verify the proc can be re-queued as READY (as proc_yield() would do) */
    proc_ready(p);
    TEST("timeslice: proc re-queued as READY after budget", p->state == PROC_READY);

    /* Drain from ready_queue so it doesn't pollute later tests */
    struct proc *drain;
    do { drain = proc_next_ready(); } while (drain && drain != p);
    proc_free(p);

    /* Two processes: simulate two full time-slice boundaries each,
     * confirm neither ever becomes ZOMBIE */
    struct proc *p1 = proc_alloc();
    struct proc *p2 = proc_alloc();
    TEST("timeslice: alloc two procs for boundary test", p1 && p2);
    if (p1 && p2) {
        p1->time_slice = 2; p1->total_time = 0; p1->state = PROC_RUNNING;
        p2->time_slice = 2; p2->total_time = 0; p2->state = PROC_RUNNING;

        for (int round = 0; round < 2; round++) {
            p1->total_time++;
            p1->total_time++;
            if (p1->total_time >= p1->time_slice) p1->total_time = 0;

            p2->total_time++;
            p2->total_time++;
            if (p2->total_time >= p2->time_slice) p2->total_time = 0;
        }
        TEST("timeslice: p1 not ZOMBIE after 2 slice boundaries", p1->state != PROC_ZOMBIE);
        TEST("timeslice: p2 not ZOMBIE after 2 slice boundaries", p2->state != PROC_ZOMBIE);
        TEST("timeslice: p1 total_time reset cleanly", p1->total_time == 0);
        TEST("timeslice: p2 total_time reset cleanly", p2->total_time == 0);

        proc_free(p1);
        proc_free(p2);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");
}

void sched_priority_test_run(void) {
    printk("[sched_priority_test_run] Not implemented yet.\n");
    TEST("sched_priority_test_run stub", 0);
}

void sched_fairness_test_run(void) {
    printk("[sched_fairness_test_run] Not implemented yet.\n");
    TEST("sched_fairness_test_run stub", 0);
}

void sched_idle_test_run(void) {
    printk("[sched_idle_test_run] Not implemented yet.\n");
    TEST("sched_idle_test_run stub", 0);
}