/*
 * kill_test.c — Kernel-level unit tests for sys_kill and pending_signals.
 *
 * Runs in RUN_TESTS mode (before schedule()).  proc_sleep() cannot be called
 * because the scheduler has never run.  We test the signal delivery mechanism
 * directly: set pending_signals, verify state, and verify that proc_wakeup
 * is triggered for BLOCKED processes.
 */

#include "trap/syscall.h"
#include "process/process.h"
#include "kernel/printk.h"
#include "tests/test.h"

static int passed = 0;
static int failed = 0;

void kill_test_run(void) {
    printk("\n========================================\n");
    printk("       kill / pending_signals Tests\n");
    printk("========================================\n\n");

    /* ── 1. sys_kill with invalid pid returns -1 ───────────────────────── */
    printk("=== sys_kill invalid args ===\n");
    int r = sys_kill(-1, 9);
    TEST("kill: invalid pid -1 returns -1", r == -1);

    r = sys_kill(99999, 9);
    TEST("kill: unknown pid returns -1", r == -1);

    r = sys_kill(1, -1);
    TEST("kill: negative signal returns -1", r == -1);

    r = sys_kill(1, 32);
    TEST("kill: signal 32 (out of range) returns -1", r == -1);

    /* ── 2. sig==0 is a validity check — no signal delivered ───────────── */
    printk("=== kill sig==0 validity check ===\n");
    struct proc *p = proc_alloc();
    TEST("kill: proc_alloc for sig=0 test", p != NULL);
    if (p) {
        p->pid  = 200;
        p->state = PROC_READY;
        p->pending_signals = 0;

        r = sys_kill(p->pid, 0);
        TEST("kill: sig=0 returns 0", r == 0);
        TEST("kill: sig=0 sets no pending_signals", p->pending_signals == 0);

        proc_free(p);
    }

    /* ── 3. SIGTERM sets the correct bit in pending_signals ────────────── */
    printk("=== kill SIGTERM (sig=15) sets bit ===\n");
    struct proc *p2 = proc_alloc();
    TEST("kill: proc_alloc for SIGTERM test", p2 != NULL);
    if (p2) {
        p2->pid   = 201;
        p2->state = PROC_READY;
        p2->pending_signals = 0;

        r = sys_kill(p2->pid, 15);   /* SIGTERM */
        TEST("kill: SIGTERM returns 0", r == 0);
        TEST("kill: SIGTERM sets bit 15", p2->pending_signals & (1U << 15));
        TEST("kill: SIGTERM does not set bit 9", !(p2->pending_signals & (1U << 9)));

        proc_free(p2);
    }

    /* ── 4. SIGKILL sets the correct bit and wakes a BLOCKED process ───── */
    printk("=== kill SIGKILL (sig=9) wakes BLOCKED proc ===\n");
    struct proc *p3 = proc_alloc();
    TEST("kill: proc_alloc for SIGKILL test", p3 != NULL);
    if (p3) {
        p3->pid   = 202;
        p3->state = PROC_BLOCKED;
        p3->pending_signals = 0;
        p3->chan  = p3;             /* sleeping on itself — mimic proc_sleep */

        /* Place in blocked_queue so proc_wakeup can find and remove it. */
        proc_block(p3);             /* proc_block sets BLOCKED + enqueues */
        /* proc_block already set state = PROC_BLOCKED above; pid/chan are set */

        r = sys_kill(p3->pid, 9);   /* SIGKILL */
        TEST("kill: SIGKILL returns 0", r == 0);
        TEST("kill: SIGKILL sets bit 9", p3->pending_signals & (1U << 9));
        /* proc_unblock (called inside sys_kill) moves the proc to READY */
        TEST("kill: BLOCKED proc moved to READY after SIGKILL", p3->state == PROC_READY);

        /* Drain p3 from ready_queue so it does not pollute later tests. */
        struct proc *drain;
        do { drain = proc_next_ready(); } while (drain && drain != p3);
        proc_free(p3);
    }

    /* ── 5. KERNEL_FATAL_SIGS mask covers SIGKILL and SIGTERM ─────────── */
    printk("=== KERNEL_FATAL_SIGS mask ===\n");
    TEST("fatal mask includes SIGKILL (9)",  KERNEL_FATAL_SIGS & (1U << 9));
    TEST("fatal mask includes SIGTERM (15)", KERNEL_FATAL_SIGS & (1U << 15));
    TEST("fatal mask excludes SIGCHLD (17)", !(KERNEL_FATAL_SIGS & (1U << 17)));

    /* ── 6. Killing a ZOMBIE process returns -1 ────────────────────────── */
    printk("=== kill on ZOMBIE returns -1 ===\n");
    struct proc *p4 = proc_alloc();
    TEST("kill: proc_alloc for zombie test", p4 != NULL);
    if (p4) {
        p4->pid   = 203;
        p4->state = PROC_ZOMBIE;
        p4->pending_signals = 0;

        r = sys_kill(p4->pid, 9);
        TEST("kill: ZOMBIE target returns -1", r == -1);
        TEST("kill: ZOMBIE pending_signals unchanged", p4->pending_signals == 0);

        proc_free(p4);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
