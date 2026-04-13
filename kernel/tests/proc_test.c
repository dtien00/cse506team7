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

static int read_user_u64(pagetable_t pt, uintptr_t uva, uint64_t *out) {
    uintptr_t pa = vmm_translate(pt, uva);
    if (!pa) return -1;
    *out = *(uint64_t *)pa;
    return 0;
}

static int user_cstr_eq(pagetable_t pt, uintptr_t uva, const char *expect) {
    for (size_t i = 0;; i++) {
        uintptr_t pa = vmm_translate(pt, uva + i);
        if (!pa) return 0;
        char got = *(char *)pa;
        if (got != expect[i]) return 0;
        if (got == '\0') return 1;
    }
}

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
        // === exec() setup test ===
    printk("=== exec() Tests ===\n");
    /*
     * Use proc_create() rather than proc_alloc() so that ep->fs, ep->vma_list,
     * ep->priority, and ep->mlfq_level are fully initialized — the same state
     * a real process has before calling exec().  proc_alloc() only memstes the
     * struct to zero and does not run any of the per-process setup code.
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
        char *eenvp[] = { "SBX_ENVP=1", "PATH=/bin", NULL };
        int r = exec("/bin/echo", eargv, eenvp);
        TEST("exec: returns 0 for valid binary", r == 0);
        TEST("exec: entry point set (pc != 0)", ep->pc != 0);
        TEST("exec: stack pointer set (sp != 0)", ep->sp != 0);
        TEST("exec: stack pointer is 16-byte aligned", (ep->sp & 0xFULL) == 0);
        TEST("exec: argc in regs[10]", ep->regs[10] == 2);
        TEST("exec: argv ptr in regs[11]", ep->regs[11] != 0);
        TEST("exec: envp ptr in regs[12]", ep->regs[12] != 0);
        TEST("exec: pagetable allocated", ep->pagetable != NULL);

        if (r == 0 && ep->pagetable) {
            uint64_t argc_slot = 0;
            uint64_t argv0 = 0, argv1 = 0, argv2 = 0;
            uint64_t env0 = 0, env1 = 0, env2 = 0;
            uintptr_t sp = ep->sp;
            uintptr_t user_argv = ep->regs[11];
            uintptr_t user_envp = ep->regs[12];

            TEST("exec: user_argv is right above argc", user_argv == sp + sizeof(uintptr_t));
            TEST("exec: user_envp follows argv[] + NULL",
                 user_envp == user_argv + (ep->regs[10] + 1) * sizeof(uintptr_t));

            TEST("exec: argc slot readable", read_user_u64(ep->pagetable, sp, &argc_slot) == 0);
            TEST("exec: argc slot value", argc_slot == 2);

            TEST("exec: argv[0] readable", read_user_u64(ep->pagetable, user_argv + 0 * sizeof(uintptr_t), &argv0) == 0);
            TEST("exec: argv[1] readable", read_user_u64(ep->pagetable, user_argv + 1 * sizeof(uintptr_t), &argv1) == 0);
            TEST("exec: argv[2] NULL readable", read_user_u64(ep->pagetable, user_argv + 2 * sizeof(uintptr_t), &argv2) == 0);
            TEST("exec: argv terminator is NULL", argv2 == 0);
            TEST("exec: argv[0] string correct", user_cstr_eq(ep->pagetable, (uintptr_t)argv0, "echo"));
            TEST("exec: argv[1] string correct", user_cstr_eq(ep->pagetable, (uintptr_t)argv1, "hello"));

            TEST("exec: envp[0] readable", read_user_u64(ep->pagetable, user_envp + 0 * sizeof(uintptr_t), &env0) == 0);
            TEST("exec: envp[1] readable", read_user_u64(ep->pagetable, user_envp + 1 * sizeof(uintptr_t), &env1) == 0);
            TEST("exec: envp[2] NULL readable", read_user_u64(ep->pagetable, user_envp + 2 * sizeof(uintptr_t), &env2) == 0);
            TEST("exec: envp terminator is NULL", env2 == 0);
            TEST("exec: envp[0] string correct", user_cstr_eq(ep->pagetable, (uintptr_t)env0, "SBX_ENVP=1"));
            TEST("exec: envp[1] string correct", user_cstr_eq(ep->pagetable, (uintptr_t)env1, "PATH=/bin"));
        }

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
        // Free the pagetable and fs_struct (don't schedule the process).
        // proc_free only handles vma_list; fs must be released separately
        // since proc_destroy is not declared in the public process.h header.
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
