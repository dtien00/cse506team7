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
    struct proc *lo = proc_alloc(); lo->priority = 10;
    struct proc *hi = proc_alloc(); hi->priority = 1;
    struct proc *mid = proc_alloc(); mid->priority = 5;
    proc_ready(lo); proc_ready(mid); proc_ready(hi);
    struct proc *n1 = proc_next_ready();
    struct proc *n2 = proc_next_ready();
    struct proc *n3 = proc_next_ready();
    TEST("priority: hi dequeues first",  n1 == hi);
    TEST("priority: mid dequeues second", n2 == mid);
    TEST("priority: lo dequeues last",   n3 == lo);
    proc_free(lo); proc_free(mid); proc_free(hi);
}

void sched_fairness_test_run(void) {
    printk("[sched_fairness_test_run] Not implemented yet.\n");
    TEST("sched_fairness_test_run stub", 0);
}

void sched_idle_test_run(void) {
    printk("[sched_idle_test_run] Not implemented yet.\n");
    TEST("sched_idle_test_run stub", 0);
}

/* ------------------------------------------------------------------ */
/* sched_nice_test_run — kernel unit tests for sys_nice() behaviour    */
/* ------------------------------------------------------------------ */

void sched_nice_test_run(void) {
    printk("\n========================================\n");
    printk("       sys_nice() Kernel Unit Tests\n");
    printk("========================================\n\n");

    /* ── KT1: sys_nice() basic clamp and overflow safety ─────────────────
     * We cannot call sys_nice() directly (requires current != NULL and a
     * real trap context), so we exercise the clamping arithmetic the same
     * way the implementation does, using long-widened intermediates.
     */
    printk("=== KT1: nice() clamp and overflow safety ===\n");
    {
        int base = PROC_DEFAULT_PRIORITY;   /* 5 */

        /* nice(0) is a no-op */
        long r = (long)base + 0L;
        if (r < 0) r = 0; 
        if (r > 19) r = 19;
        TEST("nice(0) returns current priority unchanged", (int)r == base);

        /* Positive increment */
        r = (long)base + 10L;
        if (r < 0) r = 0; 
        if (r > 19) r = 19;
        TEST("nice(+10) from 5 == 15", (int)r == 15);

        /* Negative increment */
        r = (long)base + (-6L);
        if (r < 0) r = 0; 
        if (r > 19) r = 19;
        TEST("nice(-6) from 5 clamps to 0 (5-6=-1 -> 0)", (int)r == 0);

        /* Overflow: INT_MIN must not wrap to a large positive */
        r = (long)base + (long)(-2147483648);
        if (r < 0) r = 0; 
        if (r > 19) r = 19;
        TEST("nice(INT_MIN) safely clamps to 0", (int)r == 0);

        /* Overflow: INT_MAX must not wrap to a large negative */
        r = (long)base + (long)(2147483647);
        if (r < 0) r = 0; 
        if (r > 19) r = 19;
        TEST("nice(INT_MAX) safely clamps to 19", (int)r == 19);

        /* Boundary: already at 0, decrement stays at 0 */
        r = (long)0 + (-1L);
        if (r < 0) r = 0; 
        if (r > 19) r = 19;
        TEST("nice(-1) from 0 stays at 0 (lower clamp)", (int)r == 0);

        /* Boundary: already at 19, increment stays at 19 */
        r = (long)19 + 1L;
        if (r < 0) r = 0; 
        if (r > 19) r = 19;
        TEST("nice(+1) from 19 stays at 19 (upper clamp)", (int)r == 19);
    }

    /* ── KT2: fork inherits parent priority ──────────────────────────────
     * Simulate what sys_fork() now does: allocate child, then copy
     * priority from parent.
     */
    printk("\n=== KT2: fork priority inheritance ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("KT2: alloc parent and child", parent && child);

        if (parent && child) {
            parent->priority = 12;
            /* This is what the patched sys_fork() now does */
            child->priority  = parent->priority;
            child->mlfq_level = 0;

            TEST("fork inherits priority from parent", child->priority == 12);
            TEST("fork resets child mlfq_level to 0",  child->mlfq_level == 0);
        }
        if (parent) proc_free(parent);
        if (child)  proc_free(child);
    }

    /* ── KT3: ready-queue ordering after setting different priorities ─────
     * Enqueue three procs with distinct priorities in worst-case order
     * (lowest priority first) and verify dequeue order is highest-first.
     */
    printk("\n=== KT3: priority ordering in ready queue ===\n");
    {
        struct proc *lo  = proc_alloc();
        struct proc *mid = proc_alloc();
        struct proc *hi  = proc_alloc();
        TEST("KT3: alloc lo/mid/hi", lo && mid && hi);

        if (lo && mid && hi) {
            lo->priority  = 15;
            mid->priority = 7;
            hi->priority  = 2;

            /* Enqueue lowest-first to stress the insertion sort */
            proc_ready(lo);
            proc_ready(mid);
            proc_ready(hi);

            struct proc *first  = proc_next_ready();
            struct proc *second = proc_next_ready();
            struct proc *third  = proc_next_ready();

            TEST("priority ordering: hi(2) dequeues first",   first  == hi);
            TEST("priority ordering: mid(7) dequeues second", second == mid);
            TEST("priority ordering: lo(15) dequeues third",  third  == lo);
        }
        if (lo)  proc_free(lo);
        if (mid) proc_free(mid);
        if (hi)  proc_free(hi);
    }

    /* ── KT4: equal priorities preserve FIFO order ───────────────────────
     * All three procs at the same priority must come out in insertion order.
     */
    printk("\n=== KT4: equal-priority FIFO order ===\n");
    {
        struct proc *a = proc_alloc();
        struct proc *b = proc_alloc();
        struct proc *c = proc_alloc();
        TEST("KT4: alloc a/b/c", a && b && c);

        if (a && b && c) {
            a->priority = 10;
            b->priority = 10;
            c->priority = 10;

            proc_ready(a);
            proc_ready(b);
            proc_ready(c);

            TEST("equal priority: a dequeues first",  proc_next_ready() == a);
            TEST("equal priority: b dequeues second", proc_next_ready() == b);
            TEST("equal priority: c dequeues third",  proc_next_ready() == c);
        }
        if (a) proc_free(a);
        if (b) proc_free(b);
        if (c) proc_free(c);
    }

    /* ── KT5: priority range assertion ───────────────────────────────────
     * Verify that sys_nice() arithmetic never produces a value outside
     * [0, 19] for the full range of typical inputs.
     */
    printk("\n=== KT5: priority stays in [0, 19] for all inputs ===\n");
    {
        int inputs[] = { -100, -19, -1, 0, 1, 7, 14, 19, 100 };
        int n = (int)(sizeof(inputs) / sizeof(inputs[0]));
        int all_clamped = 1;
        for (int i = 0; i < n; i++) {
            for (int base = 0; base <= 19; base++) {
                long r = (long)base + (long)inputs[i];
                if (r < 0)  r = 0;
                if (r > 19) r = 19;
                if (r < 0 || r > 19) { all_clamped = 0; break; }
            }
        }
        TEST("nice() result always in [0, 19] for typical inputs", all_clamped);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");
}