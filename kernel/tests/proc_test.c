
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
    // printk("=== Fork() Tests ===\n");
    // int test_var = 0xdeadbeef;
    // pid_t fork_result = sys_fork();
    // if (fork_result == 0) {
    //     // Child process
    //     TEST("fork: child gets 0", fork_result == 0);
    //     test_var = 0x12345678;
    //     sys_exit(42);
    // } else if (fork_result > 0) {
    //     // Parent process
    //     int status = 0;
    //     pid_t waited = sys_wait(&status);
    //     TEST("fork: parent gets child pid", waited == fork_result);
    //     TEST("fork: child exit code", status == 42);
    //     TEST("fork: memory independence", test_var == 0xdeadbeef);
    //     // Check process hierarchy
    //     struct proc *child = proc_find(waited);
    //     TEST("fork: child reaped (UNUSED)", !child || child->state == PROC_UNUSED);
    // } else {
    //     TEST("fork: failed", 0);
    // }
    // printk("=== End of Fork() Tests ===\n\n");
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

    printk("=== exec() Live Run: /bin/echo ===\n");

    // current is NULL throughout the test suite (proc_init sets it to NULL).
    // sys_fork() and sys_wait() both require a valid current process.
    // Use the init process (pid 1): proc_init already loaded /bin/echo into
    // it via exec(), so any child forked from it inherits a ready-to-run
    // /bin/echo image.  sys_fork() returns only once (to the caller with the
    // child's PID); the child starts from proc_entry() and reaches user mode
    // via proc_start_user(), so the `child_pid == 0` branch is intentionally
    // unreachable here.
    struct proc *live_saved = current;
    current = proc_find(1);

    pid_t child_pid = -1;
    if (!current) {
        TEST("exec echo: fork succeeded", 0);
    } else {
        child_pid = sys_fork();
    }

    if (child_pid > 0) {
        int exit_status = -1;
        pid_t reaped = sys_wait(&exit_status);

        TEST("exec echo: child was reaped",  reaped == child_pid);
        TEST("exec echo: clean exit (0)",    exit_status == 0);

    } else if (child_pid == 0) {
        // Unreachable with current sys_fork() semantics, but guard it.
        sys_exit(1);
    }

    current = live_saved;

    printk("=== End of exec() Live Run ===\n\n");

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");

}
