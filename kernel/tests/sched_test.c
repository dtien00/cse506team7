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
    printk("[sched_timeslice_test_run] Not implemented yet.\n");
    TEST("sched_timeslice_test_run stub", 0);
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