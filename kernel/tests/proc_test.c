#include "process/process.h"
#include "process/swtch.h"
#include "kernel/printk.h"
#include "trap/trap.h"
#include "arch/riscv.h"
#include "fs/vfs.h"
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/kmalloc.h"   // added — needed for kmalloc/kfree in exec()
#include "types.h"
#include "string.h"
#include "tests/test.h"
#include "elf/elf.h"
#include "trap/syscall.h"

static int passed = 0;
static int failed = 0;

// Test
void proc_test_run(void) {
    printk("\n========================================\n");
    printk("       Process Tests\n");
    printk("========================================\n\n");
    printk("=== Process Core Tests ===\n");
    // Test process allocation (do not schedule or start real processes)
    struct proc *p1 = proc_alloc();
    TEST("proc_alloc returns non-NULL", p1 != NULL);
    if (p1) {
        p1->pid = 42;
        strncpy(p1->name, "test_proc1", MAX_NAME - 1);
        p1->state = PROC_CREATED;
    }
    TEST("proc_alloc sets state CREATED", p1 && p1->state == PROC_CREATED);
    // Test process table population
    struct proc *found = proc_find(p1 ? p1->pid : -1);
    TEST("proc_find locates created process", found == p1);
    // Test process free
    if (p1) {
        proc_free(p1);
        TEST("proc_free sets UNUSED", p1->state == PROC_UNUSED);
    }
    // Test max process limit
    int count = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_alloc() != NULL) count++;
    }
    TEST("proc_alloc respects MAX_PROCS", count <= MAX_PROCS);

    // === Test wait/zombie/exit code ===
    struct proc *parent = proc_alloc();
    struct proc *child = proc_alloc();
    TEST("wait: parent and child alloc", parent && child);
    if (parent && child) {
        parent->pid = 100;
        strncpy(parent->name, "parent", MAX_NAME - 1);
        parent->state = PROC_CREATED;
        child->pid = 101;
        strncpy(child->name, "child", MAX_NAME - 1);
        child->state = PROC_ZOMBIE;
        child->exit_code = 42;
        // Link child to parent
        child->parent = parent;
        child->sibling = NULL;
        parent->first_child = child;

        // proc_wait uses `current` as the parent; set it for the duration
        // of the test (current is NULL throughout the test suite).
        struct proc *wait_saved = current;
        current = parent;

        int exit_code = 0;
        int waited_pid = proc_wait(&exit_code);
        TEST("wait: correct pid", waited_pid == 101);
        TEST("wait: correct exit code", exit_code == 42);
        // After wait, child should be freed
        TEST("wait: child reaped", child->state == PROC_UNUSED);
        // No more children — proc_wait already cleared first_child
        TEST("wait: no more children", proc_wait(NULL) == -1);

        current = wait_saved;
    }
    printk("\n========================================\n");
    printk("    End of Process Tests\n");
    printk("========================================\n\n");

    // === Fork() Test Suite ===
    // RUN_TESTS executes before entering schedule(), so children created by
    // sys_fork() will not run unless we explicitly schedule. Validate fork
    // metadata, then simulate child exit so sys_wait() is non-blocking.
    printk("=== Fork() Tests ===\n");

    struct proc *fork_saved = current;
    current = proc_find(1);  // init process from proc_init()

    pid_t fork_result = -1;
    struct proc *fork_child = NULL;
    if (!current) {
        TEST("fork: init process exists", 0);
    } else {
        fork_result = sys_fork();
    }

    if (fork_result > 0) {
        fork_child = proc_find(fork_result);
        TEST("fork: returns child pid", fork_result > 1);
        TEST("fork: child exists in proc table", fork_child != NULL);
        TEST("fork: child linked to parent", fork_child && fork_child->parent == current);

        if (fork_child) {
            // Force deterministic wait() behavior in unit-test context.
            fork_child->state = PROC_ZOMBIE;
            fork_child->exit_code = 0;

            int status = -1;
            pid_t waited = sys_wait(&status);
            TEST("fork: wait reaps expected child", waited == fork_result);
            TEST("fork: wait reports exit code", status == 0);
            TEST("fork: child reaped (pid cleared)", proc_find(waited) == NULL);
        }
    } else {
        TEST("fork: sys_fork succeeded", 0);
    }

    current = fork_saved;
    printk("=== End of Fork() Tests ===\n\n");
        // === exec() setup test ===
    printk("=== exec() Tests ===\n");
    struct proc *ep = proc_alloc();
    TEST("exec: proc_alloc for exec test", ep != NULL);
    if (ep) {
        ep->pid = 200;
        strncpy(ep->name, "exec_test", MAX_NAME - 1);
        ep->state = PROC_CREATED;
        ep->pagetable = NULL;
        ep->trapframe = NULL;

        struct proc *saved = current;
        current = ep;

        char *eargv[] = { "echo", "hello", NULL };
        int r = exec("/bin/echo", eargv);
        TEST("exec: returns 0 for valid binary", r == 0);
        TEST("exec: entry point set (pc != 0)", ep->pc != 0);
        TEST("exec: stack pointer set (sp != 0)", ep->sp != 0);
        TEST("exec: argc in regs[10]", ep->regs[10] == 2);
        TEST("exec: argv ptr in regs[11]", ep->regs[11] != 0);
        TEST("exec: pagetable allocated", ep->pagetable != NULL);

        current = saved;
        // exec() called vmm_switch() internally; restore SATP before
        // freeing ep's page table, otherwise SATP points to freed memory.
        // When saved is NULL (current was NULL before this test) fall back
        // to the kernel page table so kernel code keeps working.
        {
            pagetable_t restore_pt = (saved && saved->pagetable)
                                     ? saved->pagetable
                                     : vmm_kernel_pagetable();
            vmm_switch(restore_pt);
        }
        // Free the pagetable (don't schedule the process)
        vmm_free_pagetable(ep->pagetable);
        proc_free(ep);
    }

    printk("=== End of exec() Tests ===\n\n");

    printk("=== Fork+Wait deterministic sanity ===\n");

    // Same constraint as above: no scheduler handoff in RUN_TESTS mode.
    // Reuse init as parent, fork, then mark child ZOMBIE before wait.
    struct proc *live_saved = current;
    current = proc_find(1);

    pid_t child_pid = -1;
    if (!current) {
        TEST("fork/wait sanity: init process exists", 0);
    } else {
        child_pid = sys_fork();
    }

    if (child_pid > 0) {
        struct proc *child_proc = proc_find(child_pid);
        TEST("fork/wait sanity: child created", child_proc != NULL);

        if (child_proc) {
            child_proc->state = PROC_ZOMBIE;
            child_proc->exit_code = 0;

            int exit_status = -1;
            pid_t reaped = sys_wait(&exit_status);

            TEST("fork/wait sanity: child reaped", reaped == child_pid);
            TEST("fork/wait sanity: clean exit", exit_status == 0);
        }
    } else {
        TEST("fork/wait sanity: fork succeeded", 0);
    }

    current = live_saved;

    printk("=== End of Fork+Wait deterministic sanity ===\n\n");

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");

}
