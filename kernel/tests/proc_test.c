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
#include "lib/string.h"
#include "tests/test.h"
#include "elf/elf.h"
#include "trap/syscall.h"
#include "process/process.h"

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
    struct proc *test_procs[MAX_PROCS];
    int count = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        test_procs[i] = proc_alloc();
        if (test_procs[i] != NULL) count++;
    }
    TEST("proc_alloc respects MAX_PROCS", count <= MAX_PROCS);

    // Free all test allocations before continuing
    for (int i = 0; i < MAX_PROCS; i++) {
        if (test_procs[i]) proc_free(test_procs[i]);
    }

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
        TEST("wait: correct exit code", exit_code == (42 << 8));
        // After wait, child should be freed
        TEST("wait: child reaped", child->state == PROC_UNUSED);
        // No more children — proc_wait already cleared first_child
        TEST("wait: no more children", proc_wait(NULL) == -1);

        current = wait_saved;
        proc_free(parent);  // must free parent; leaving it in proctab pollutes later tests
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
            // sys_fork() enqueued the child via proc_ready(); drain it from
            // the MLFQ before manually transitioning to ZOMBIE so that
            // proc_free() (inside sys_wait) doesn't leave a stale MLFQ pointer.
            { struct proc *d; do { d = proc_next_ready(); } while (d && d != fork_child); }

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
    // === exec() Tests ===
    printk("=== exec() Tests ===\n");
    /*
     * Use proc_create() so ep->fs, ep->vma_list, ep->priority, and
     * ep->mlfq_level are fully initialized — the same state a real process
     * has before exec().
     */
    struct proc *ep = proc_create("exec_test");
    TEST("exec: proc_create for exec test", ep != NULL);
    if (ep) {
        ep->fs = fs_struct_alloc();
        if (ep->fs) {
            fs_struct_set_root(ep->fs, "/");
            fs_struct_set_cwd(ep->fs, "/");
        }
        ep->trapframe = NULL;

        struct proc *saved = current;
        current = ep;

        char *eargv[] = { "echo", "hello", NULL };
        char *eenvp[] = { "TEST=1", "PATH=/bin", NULL };
        int r = exec("/bin/echo", eargv, eenvp);
        TEST("exec: returns 0 for valid binary",  r == 0);
        TEST("exec: entry point set (pc != 0)",   ep->pc != 0);
        TEST("exec: stack pointer set (sp != 0)", ep->sp != 0);
        TEST("exec: sp is 16-byte aligned",       (ep->sp & 0xF) == 0);
        TEST("exec: argc in regs[10]",            ep->regs[10] == 2);
        TEST("exec: argv ptr in regs[11]",        ep->regs[11] != 0);
        TEST("exec: envp ptr in regs[12]",        ep->regs[12] != 0);
        TEST("exec: pagetable allocated",         ep->pagetable != NULL);

        /* Verify stack layout via physical address translation */

        /* *(sp + 0) must equal argc (2) */
        {
            uintptr_t argc_pa = vmm_translate(ep->pagetable, ep->sp);
            TEST("exec: argc word on stack == 2",
                 argc_pa != 0 && *(uintptr_t *)argc_pa == 2);
        }

        /* *(sp + 8) is argv[0] VA; translating it gives the string "echo" */
        {
            uintptr_t argv0_slot_pa = vmm_translate(ep->pagetable, ep->sp + 8);
            int ok = 0;
            if (argv0_slot_pa) {
                uintptr_t argv0_va = *(uintptr_t *)argv0_slot_pa;
                uintptr_t argv0_pa = vmm_translate(ep->pagetable, argv0_va);
                ok = (argv0_pa != 0) && (strcmp((char *)argv0_pa, "echo") == 0);
            }
            TEST("exec: argv[0] string == 'echo'", ok);
        }

        /* argv[argc] (NULL terminator) must be 0: at sp+8 + argc*8 = sp+24 */
        {
            uintptr_t argv_null_pa = vmm_translate(ep->pagetable,
                                                   ep->sp + 8 + 2 * sizeof(uintptr_t));
            TEST("exec: argv NULL terminator == 0",
                 argv_null_pa != 0 && *(uintptr_t *)argv_null_pa == 0);
        }

        /* regs[12] points to envp[0] VA slot; translating gives "TEST=1" */
        {
            uintptr_t envp0_slot_pa = vmm_translate(ep->pagetable, ep->regs[12]);
            int ok = 0;
            if (envp0_slot_pa) {
                uintptr_t envp0_va = *(uintptr_t *)envp0_slot_pa;
                uintptr_t envp0_pa = vmm_translate(ep->pagetable, envp0_va);
                ok = (envp0_pa != 0) && (strcmp((char *)envp0_pa, "TEST=1") == 0);
            }
            TEST("exec: envp[0] string == 'TEST=1'", ok);
        }

        /* envp NULL terminator: at regs[12] + envc*8 = regs[12] + 16 */
        {
            uintptr_t envp_null_pa = vmm_translate(ep->pagetable,
                                                   ep->regs[12] + 2 * sizeof(uintptr_t));
            TEST("exec: envp NULL terminator == 0",
                 envp_null_pa != 0 && *(uintptr_t *)envp_null_pa == 0);
        }

        current = saved;
        /* exec() called vmm_switch() internally; restore SATP before freeing
         * ep's page table, otherwise SATP points to freed memory.           */
        {
            pagetable_t restore_pt = (saved && saved->pagetable)
                                     ? saved->pagetable
                                     : vmm_kernel_pagetable();
            vmm_switch(restore_pt);
        }
        vmm_free_pagetable(ep->pagetable);
        ep->pagetable = NULL;
        if (ep->fs) {
            fs_struct_free(ep->fs);
            ep->fs = NULL;
        }
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
            // Same as above: drain child from MLFQ before manual ZOMBIE transition.
            { struct proc *d; do { d = proc_next_ready(); } while (d && d != child_proc); }

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

static void __attribute__((noinline)) recursive_overflow(int depth) {
    volatile uint8_t buf[256];
    buf[0] = (uint8_t)depth;
    if (depth < 10000)
        recursive_overflow(depth + 1);
    buf[0]++;  // use buf after recursive call — forces frame to survive, blocks tail-call opt
}

static void __attribute__((noreturn)) overflow_entry(void) {
    recursive_overflow(0);
    while (1) asm volatile("wfi");
}

void kstack_overflow_test_run(void) {
    printk("\n========================================\n");
    printk("  Kernel Stack Overflow Test (DESTRUCTIVE)\n");
    printk("  Expected: KERNEL STACK OVERFLOW panic\n");
    printk("========================================\n");

    struct proc *p = proc_create("kstack_ovf");
    if (!p) {
        printk("  [SKIP] could not create overflow process\n");
        return;
    }

    current = p;
    p->state = PROC_RUNNING;
    // Give p a pagetable so vmm_translate doesn't crash in page_fault_handler.
    // The kernel pt has the guard page unmapped, so the lookup returns 0 as expected.
    // proc_free is never called for this process — the test panics.
    p->pagetable = vmm_kernel_pagetable();

    uint64_t kstack_top = (uint64_t)(p->kstack + KSTACK_SIZE);

    // sscratch must be non-zero when the guard page fault fires in S-mode.
    // trap_entry swaps sp ↔ sscratch on entry; with sscratch=0 it would restore
    // the already-overflowed sp and then fault again trying to allocate the trap
    // frame — a silent nested-fault loop. Pointing sscratch at the kstack top
    // gives the trap handler a valid stack to save the frame, while SPP=1 in
    // the saved sstatus still correctly identifies this as an S-mode fault.
    write_csr(sscratch, kstack_top);

    // Switch sp directly to p's kstack and jump to overflow_entry.
    // Bypassing the scheduler avoids sscratch races with the init process.
    asm volatile(
        "mv sp, %0\n"
        "jr %1\n"
        :: "r"(kstack_top), "r"((uint64_t)overflow_entry)
        : "memory"
    );
    __builtin_unreachable();
}

// Tests for the extended proc_waitpid() POSIX compliance:
//   - pid == -1 delegates to proc_wait() (any child)
//   - WNOHANG (options & 1) returns 0 instead of blocking when no zombie found
//   - WUNTRACED | WNOHANG and WCONTINUED | WNOHANG are silently accepted
//   - Non-child PID returns -1
// All tests are deterministic (no scheduler handoff); proc_sleep() is never called.
void proc_waitpid_test_run(void) {
    printk("\n========================================\n");
    printk("       proc_waitpid() POSIX Tests\n");
    printk("========================================\n\n");

    struct proc *wp_saved = current;

    // Set up a fake parent so proc_waitpid can walk first_child.
    struct proc *parent = proc_alloc();
    TEST("waitpid: parent alloc", parent != NULL);
    if (!parent) {
        current = wp_saved;
        return;
    }
    parent->state = PROC_CREATED;
    current = parent;

    // --- Test 1: specific PID — zombie child is reaped ---
    printk("=== Test: specific PID zombie reap ===\n");
    struct proc *zombie = proc_alloc();
    TEST("waitpid: zombie child alloc", zombie != NULL);
    if (zombie) {
        zombie->state = PROC_ZOMBIE;
        zombie->exit_code = 7;
        zombie->parent = parent;
        zombie->sibling = NULL;
        parent->first_child = zombie;
        pid_t zid = zombie->pid;

        int code = -1;
        pid_t r = proc_waitpid(zid, &code, 0);
        TEST("waitpid: returns child pid", r == zid);
        // POSIX wait-status: exit code packed as (code & 0xff) << 8
        TEST("waitpid: exit code encoded correctly", code == (7 << 8));
        TEST("waitpid: child reaped (UNUSED)", zombie->state == PROC_UNUSED);
        parent->first_child = NULL;
    }

    // --- Test 2: pid == -1 delegates to proc_wait() (any child) ---
    printk("=== Test: pid == -1 delegates to any-child ===\n");
    struct proc *any_child = proc_alloc();
    TEST("waitpid: any-child alloc", any_child != NULL);
    if (any_child) {
        any_child->state = PROC_ZOMBIE;
        any_child->exit_code = 3;
        any_child->parent = parent;
        any_child->sibling = NULL;
        parent->first_child = any_child;
        pid_t acid = any_child->pid;

        int code = -1;
        pid_t r = proc_waitpid(-1, &code, 0);
        TEST("waitpid(-1): returns child pid", r == acid);
        TEST("waitpid(-1): exit code correct", code == (3 << 8));
        TEST("waitpid(-1): child reaped", any_child->state == PROC_UNUSED);
        parent->first_child = NULL;
    }

    // --- Test 3: WNOHANG — live (non-zombie) child returns 0, doesn't block ---
    printk("=== Test: WNOHANG on live child ===\n");
    struct proc *live = proc_alloc();
    TEST("waitpid: live child alloc", live != NULL);
    if (live) {
        live->state = PROC_READY;
        live->parent = parent;
        live->sibling = NULL;
        parent->first_child = live;

        pid_t r = proc_waitpid(live->pid, NULL, 1 /* WNOHANG */);
        TEST("waitpid WNOHANG: returns 0 (no block)", r == 0);
        TEST("waitpid WNOHANG: child still alive", live->state != PROC_UNUSED);

        parent->first_child = NULL;
        proc_free(live);
    }

    // --- Test 4: non-child PID returns -1 ---
    printk("=== Test: non-child PID returns -1 ===\n");
    parent->first_child = NULL;
    pid_t r4 = proc_waitpid(9999, NULL, 0);
    TEST("waitpid: non-child returns -1", r4 == -1);

    // --- Test 5: WUNTRACED|WNOHANG and WCONTINUED|WNOHANG silently accepted ---
    // No stopped/continued processes exist yet; WNOHANG fires and returns 0.
    printk("=== Test: WUNTRACED/WCONTINUED flags silently accepted ===\n");
    struct proc *live2 = proc_alloc();
    TEST("waitpid: live2 child alloc", live2 != NULL);
    if (live2) {
        live2->state = PROC_READY;
        live2->parent = parent;
        live2->sibling = NULL;
        parent->first_child = live2;

        // WUNTRACED (2) | WNOHANG (1) = 3
        pid_t r5a = proc_waitpid(live2->pid, NULL, 2 | 1);
        TEST("waitpid WUNTRACED|WNOHANG: returns 0 (not -1, not crash)", r5a == 0);

        // WCONTINUED (8) | WNOHANG (1) = 9
        pid_t r5b = proc_waitpid(live2->pid, NULL, 8 | 1);
        TEST("waitpid WCONTINUED|WNOHANG: returns 0 (not -1, not crash)", r5b == 0);

        parent->first_child = NULL;
        proc_free(live2);
    }

    proc_free(parent);
    current = wp_saved;

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}

/*
 * procinfo_test_run — kernel-side unit tests for sys_procinfo.
 *
 * Runs before the scheduler starts, so we manipulate proctab directly
 * rather than relying on running processes.  At minimum, proc_init()
 * has already inserted init (pid 1) into the table.
 */
void procinfo_test_run(void) {
    printk("\n========================================\n");
    printk("       sys_procinfo Tests\n");
    printk("========================================\n\n");

    struct proc_info buf[MAX_PROCS];

    /* ── max_count=0: must return 0, must not touch the buffer ───────── */
    int r0 = sys_procinfo(buf, 0);
    TEST("procinfo: max_count=0 returns 0", r0 == 0);

    /* ── normal call: at least init is present ───────────────────────── */
    int n = sys_procinfo(buf, MAX_PROCS);
    TEST("procinfo: returns > 0 (at least init)", n > 0);

    /* ── init process (pid 1) appears in the output ─────────────────── */
    int init_found = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i].pid == 1) { init_found = 1; break; }
    }
    TEST("procinfo: init (pid=1) found in output", init_found);

    /* ── all returned states are non-zero (PROC_UNUSED filtered out) ─── */
    int all_states_nonzero = 1;
    for (int i = 0; i < n; i++) {
        if (buf[i].state == 0) { all_states_nonzero = 0; break; }
    }
    TEST("procinfo: no PROC_UNUSED entries returned", all_states_nonzero);

    /* ── all returned PIDs positive ──────────────────────────────────── */
    int all_pids_ok = 1;
    for (int i = 0; i < n; i++) {
        if (buf[i].pid <= 0) { all_pids_ok = 0; break; }
    }
    TEST("procinfo: all PIDs positive", all_pids_ok);

    /* ── max_count=1: returns exactly 1 ─────────────────────────────── */
    struct proc_info one;
    int r1 = sys_procinfo(&one, 1);
    TEST("procinfo: max_count=1 returns exactly 1", r1 == 1);

    /* ── count increases after proc_alloc ───────────────────────────── */
    struct proc *extra = proc_alloc();
    if (extra) {
        extra->state = PROC_CREATED;
        int n2 = sys_procinfo(buf, MAX_PROCS);
        TEST("procinfo: count increases after proc_alloc", n2 == n + 1);
        proc_free(extra);
        int n3 = sys_procinfo(buf, MAX_PROCS);
        TEST("procinfo: count back to baseline after proc_free", n3 == n);
    } else {
        TEST("procinfo: proc_alloc for count test", 0);
        TEST("procinfo: count back to baseline after proc_free", 0);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
