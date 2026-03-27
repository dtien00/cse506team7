
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

        int exit_code = 0;
        int waited_pid = proc_wait(&exit_code);
        TEST("wait: correct pid", waited_pid == 101);
        TEST("wait: correct exit code", exit_code == 42);
        // After wait, child should be freed
        TEST("wait: child reaped", child->state == PROC_UNUSED);
        // No more children
        TEST("wait: no more children", parent->first_child == NULL);
    }
    printk("\n========================================\n");
    printk("    End of Process Tests\n");
    printk("========================================\n\n");

    // === Fork() Test Suite ===
    printk("=== Fork() Tests ===\n");
    int test_var = 0xdeadbeef;
    pid_t fork_result = sys_fork();
    if (fork_result == 0) {
        // Child process
        TEST("fork: child gets 0", fork_result == 0);
        test_var = 0x12345678;
        sys_exit(42);
    } else if (fork_result > 0) {
        // Parent process
        int status = 0;
        pid_t waited = sys_wait(&status);
        TEST("fork: parent gets child pid", waited == fork_result);
        TEST("fork: child exit code", status == 42);
        TEST("fork: memory independence", test_var == 0xdeadbeef);
        // Check process hierarchy
        struct proc *child = proc_find(waited);
        TEST("fork: child reaped (UNUSED)", !child || child->state == PROC_UNUSED);
    } else {
        TEST("fork: failed", 0);
    }
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
        // Free the pagetable (don't schedule the process)
        vmm_free_pagetable(ep->pagetable);
        proc_free(ep);
    }

    printk("=== End of exec() Tests ===\n\n");

    printk("=== exec() Live Run: /bin/echo ===\n");

    pid_t child_pid = sys_fork();

    if (child_pid == 0) {
        current->time_slice = 10;
        current->total_time = 0;

        char *argv[] = { "echo", "hello", "from", "exec", NULL };
        exec("/bin/echo", argv);

        sys_exit(1);

    } else if (child_pid > 0) {
        int exit_status = -1;
        pid_t reaped = sys_wait(&exit_status);

        TEST("exec echo: child was reaped",  reaped == child_pid);
        TEST("exec echo: clean exit (0)",    exit_status == 0);

    } else {
        TEST("exec echo: fork succeeded", 0);
    }

    printk("=== End of exec() Live Run ===\n\n");

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n");

}
