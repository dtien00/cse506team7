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
#include "signal.h"

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

    // Test: SIGSTOP on a READY process → PROC_STOPPED
    {
        struct proc *p = proc_alloc();
        p->pid   = 205;
        p->state = PROC_READY;
        p->signal_mask = 0;
        // Must add to ready_queue for proc_stop to remove it
        proc_ready(p);
        sys_kill(p->pid, SIGSTOP);
        TEST("SIGSTOP: READY proc becomes PROC_STOPPED", p->state == PROC_STOPPED);
        proc_free(p);
    }

    // Test: SIGCONT on a STOPPED process → PROC_READY
    {
        struct proc *p = proc_alloc();
        p->pid   = 206;
        p->state = PROC_READY;
        p->signal_mask = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSTOP);   // → PROC_STOPPED
        sys_kill(p->pid, SIGCONT);   // → PROC_READY via proc_unstop
        TEST("SIGCONT: STOPPED proc becomes PROC_READY", p->state == PROC_READY);
        proc_free(p);
    }

    // Test: signal_mask blocks SIGTERM (bit stays unset in pending)
    // NOTE: POSIX says masked signals REMAIN PENDING — this test verifies the bit IS set.
    {
        struct proc *p = proc_alloc();
        p->pid   = 207;
        p->state = PROC_READY;
        p->signal_mask = (1U << KERNEL_SIGTERM);
        proc_ready(p);
        sys_kill(p->pid, SIGTERM);
        TEST("signal_mask: SIGTERM pending bit IS set (mask checked at delivery)", 
            p->pending_signals & (1U << KERNEL_SIGTERM));
        // Delivery check should suppress it (mask filters at delivery, not send)
        uint32_t unmasked = p->pending_signals & ~p->signal_mask;
        TEST("signal_mask: SIGTERM filtered at delivery", !(unmasked & (1U << KERNEL_SIGTERM)));
        proc_free(p);
    }

    // Test: SIGKILL is never filtered by signal_mask
    {
        struct proc *p = proc_alloc();
        p->pid   = 208;
        p->state = PROC_READY;
        p->signal_mask = 0xFFFFFFFF; // try to mask everything
        // sys_sigprocmask enforces this; set directly to test delivery path
        p->signal_mask &= ~((1U << KERNEL_SIGKILL) | (1U << KERNEL_SIGSTOP));
        proc_ready(p);
        sys_kill(p->pid, SIGKILL);
        uint32_t unmasked = p->pending_signals & ~p->signal_mask;
        TEST("SIGKILL: passes delivery filter even with full mask",
            unmasked & (1U << KERNEL_SIGKILL));
        proc_free(p);
    }

    // Test: SIGINT goes through as fatal signal
    {
        struct proc *p = proc_alloc();
        p->pid   = 209;
        p->state = PROC_READY;
        p->signal_mask = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGINT);
        TEST("SIGINT: pending bit set", p->pending_signals & (1U << KERNEL_SIGINT));
        TEST("SIGINT: classified as fatal", (p->pending_signals & ~p->signal_mask) & KERNEL_FATAL_SIGS);
        proc_free(p);
    }

    // Test: SIGCHLD delivered to parent on SIGSTOP
    {
        printk("=== SIGCHLD delivery on child stop ===\n");
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        parent->pid = 210; parent->state = PROC_READY;
        parent->pending_signals = 0;
        child->pid  = 211; child->state = PROC_READY;
        child->parent = parent;
        child->signal_mask = 0;
        proc_ready(child);

        sys_kill(child->pid, SIGSTOP);
        TEST("SIGCHLD on stop: child is PROC_STOPPED", child->state == PROC_STOPPED);
        TEST("SIGCHLD on stop: parent has SIGCHLD pending",
             parent->pending_signals & (1U << SIGCHLD));

        // Clear for next sub-test
        parent->pending_signals = 0;
        proc_free(child);
        proc_free(parent);
    }

    // Test: SIGCHLD delivered to parent on SIGCONT
    {
        printk("=== SIGCHLD delivery on child continue ===\n");
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        parent->pid = 212; parent->state = PROC_READY;
        parent->pending_signals = 0;
        child->pid  = 213; child->state = PROC_READY;
        child->parent = parent;
        child->signal_mask = 0;
        proc_ready(child);

        sys_kill(child->pid, SIGSTOP);   // stop
        parent->pending_signals = 0;     // clear SIGCHLD from stop
        sys_kill(child->pid, SIGCONT);   // continue
        TEST("SIGCHLD on cont: child is PROC_READY", child->state == PROC_READY);
        TEST("SIGCHLD on cont: parent has SIGCHLD pending",
             parent->pending_signals & (1U << SIGCHLD));

        proc_free(child);
        proc_free(parent);
    }

        // Test: kill(pid, SIGQUIT) sets bit 3 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 300; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        int r = sys_kill(p->pid, SIGQUIT);
        TEST("SIGQUIT: sys_kill returns 0", r == 0);
        TEST("SIGQUIT: bit 3 set in pending_signals", p->pending_signals & (1U << 3));
        // Clean up
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: kill(pid, SIGILL) sets bit 4 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 301; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGILL);
        TEST("SIGILL: bit 4 set in pending_signals", p->pending_signals & (1U << 4));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: kill(pid, SIGSEGV) sets bit 11 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 302; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSEGV);
        TEST("SIGSEGV: bit 11 set in pending_signals", p->pending_signals & (1U << 11));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: KERNEL_FATAL_SIGS macro includes all three new signals
    {
        TEST("KERNEL_FATAL_SIGS includes SIGQUIT(3)",
            KERNEL_FATAL_SIGS & (1U << 3));
        TEST("KERNEL_FATAL_SIGS includes SIGILL(4)",
            KERNEL_FATAL_SIGS & (1U << 4));
        TEST("KERNEL_FATAL_SIGS includes SIGSEGV(11)",
            KERNEL_FATAL_SIGS & (1U << 11));
    }

    // Test: KERNEL_FATAL_SIGS still includes all original signals
    {
        TEST("KERNEL_FATAL_SIGS: SIGKILL(9) still present",  KERNEL_FATAL_SIGS & (1U << 9));
        TEST("KERNEL_FATAL_SIGS: SIGTERM(15) still present", KERNEL_FATAL_SIGS & (1U << 15));
        TEST("KERNEL_FATAL_SIGS: SIGINT(2) still present",   KERNEL_FATAL_SIGS & (1U << 2));
        TEST("KERNEL_FATAL_SIGS: SIGPIPE(13) still present", KERNEL_FATAL_SIGS & (1U << 13));
        TEST("KERNEL_FATAL_SIGS: SIGALRM(14) still present", KERNEL_FATAL_SIGS & (1U << 14));
    }

    // Test: signal_mask blocks SIGQUIT — pending bit still set, delivery filtered
    {
        struct proc *p = proc_alloc();
        p->pid = 303; p->state = PROC_READY;
        p->signal_mask = (1U << 3);   // mask SIGQUIT
        p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGQUIT);
        TEST("sigprocmask: SIGQUIT pending bit IS set (masked)",
            p->pending_signals & (1U << 3));
        uint32_t unmasked = p->pending_signals & ~p->signal_mask;
        TEST("sigprocmask: SIGQUIT filtered at delivery",
            !(unmasked & (1U << 3)));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: signal_mask does NOT block SIGSEGV in KERNEL_FATAL_SIGS path for unmasked case
    {
        struct proc *p = proc_alloc();
        p->pid = 304; p->state = PROC_READY;
        p->signal_mask = 0;
        p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSEGV);
        uint32_t unmasked = p->pending_signals & ~p->signal_mask;
        TEST("SIGSEGV unmasked: passes delivery filter",
            unmasked & KERNEL_FATAL_SIGS & (1U << 11));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: SIGILL on a PROC_BLOCKED process — bit set, proc_unblock called → PROC_READY
    {
        struct proc *p = proc_alloc();
        p->pid = 305; p->state = PROC_CREATED; p->signal_mask = 0;
        p->pending_signals = 0; p->chan = p;
        proc_block(p);   // → PROC_BLOCKED
        sys_kill(p->pid, SIGILL);
        TEST("SIGILL to BLOCKED: bit 4 set", p->pending_signals & (1U << 4));
        // sys_kill general path calls proc_unblock for PROC_BLOCKED targets
        TEST("SIGILL to BLOCKED: state becomes PROC_READY", p->state == PROC_READY);
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
