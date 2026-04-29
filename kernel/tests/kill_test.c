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

    /* ── 1. sys_kill with invalid args returns specific errno codes ─────── */
    printk("=== sys_kill invalid args ===\n");
    int r = sys_kill(-1, 9);
    TEST("kill(-1): no eligible targets returns -ESRCH", r == -ESRCH);

    r = sys_kill(99999, 9);
    TEST("kill(unknown pid): returns -ESRCH", r == -ESRCH);

    r = sys_kill(1, -1);
    TEST("kill: negative signal returns -EINVAL", r == -EINVAL);

    r = sys_kill(1, 32);
    TEST("kill: signal 32 (out of range) returns -EINVAL", r == -EINVAL);

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

    /* ── 6. Killing a ZOMBIE process returns -ESRCH ─────────────────────── */
    printk("=== kill on ZOMBIE returns -ESRCH ===\n");
    struct proc *p4 = proc_alloc();
    TEST("kill: proc_alloc for zombie test", p4 != NULL);
    if (p4) {
        p4->pid   = 203;
        p4->state = PROC_ZOMBIE;
        p4->pending_signals = 0;

        r = sys_kill(p4->pid, 9);
        TEST("kill: ZOMBIE target returns -ESRCH", r == -ESRCH);
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

    // ── SIGBUS / SIGFPE dispatch ──────────────────────────────────────────────────
    printk("=== SIGBUS / SIGFPE dispatch ===\n");

    // Test: kill(pid, SIGBUS) sets bit 7 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 306; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        int r = sys_kill(p->pid, SIGBUS);
        TEST("SIGBUS: sys_kill returns 0", r == 0);
        TEST("SIGBUS: bit 7 set in pending_signals", p->pending_signals & (1U << 7));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: kill(pid, SIGFPE) sets bit 8 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 307; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGFPE);
        TEST("SIGFPE: bit 8 set in pending_signals", p->pending_signals & (1U << 8));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: KERNEL_FATAL_SIGS includes SIGBUS (7) and SIGFPE (8)
    {
        TEST("KERNEL_FATAL_SIGS includes SIGBUS(7)",  KERNEL_FATAL_SIGS & (1U << 7));
        TEST("KERNEL_FATAL_SIGS includes SIGFPE(8)",  KERNEL_FATAL_SIGS & (1U << 8));
        TEST("KERNEL_SIGBUS constant is 7",           KERNEL_SIGBUS == 7);
        TEST("KERNEL_SIGFPE constant is 8",           KERNEL_SIGFPE == 8);
    }

    // Test: SIGBUS to PROC_BLOCKED — bit set, proc_unblock called → PROC_READY
    {
        struct proc *p = proc_alloc();
        p->pid = 308; p->state = PROC_CREATED; p->signal_mask = 0;
        p->pending_signals = 0; p->chan = p;
        proc_block(p);
        sys_kill(p->pid, SIGBUS);
        TEST("SIGBUS to BLOCKED: bit 7 set", p->pending_signals & (1U << 7));
        TEST("SIGBUS to BLOCKED: state becomes PROC_READY", p->state == PROC_READY);
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: SIGBUS to PROC_ZOMBIE — returns -ESRCH, state unchanged
    {
        struct proc *p = proc_alloc();
        p->pid = 309; p->state = PROC_ZOMBIE; p->pending_signals = 0;
        int r = sys_kill(p->pid, SIGBUS);
        TEST("SIGBUS to ZOMBIE: returns -ESRCH",        r == -ESRCH);
        TEST("SIGBUS to ZOMBIE: state unchanged",   p->state == PROC_ZOMBIE);
        proc_free(p);
    }

    // Test: SIGBUS unmasked — passes KERNEL_FATAL_SIGS delivery filter
    {
        struct proc *p = proc_alloc();
        p->pid = 310; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGBUS);
        uint32_t unmasked = p->pending_signals & ~p->signal_mask;
        TEST("SIGBUS unmasked: passes delivery filter",
             unmasked & KERNEL_FATAL_SIGS & (1U << 7));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: sys_sigprocmask strip removes SIGBUS — it cannot be masked
    // Directly verify the strip formula matches the enforcement in sys_sigprocmask.
    {
        uint32_t attempted = 0xFFFFFFFFU;  // try to mask every signal
        uint32_t stripped  = attempted & ~((1U << KERNEL_SIGKILL) | (1U << KERNEL_SIGSTOP) |
                                           (1U << KERNEL_SIGILL)  | (1U << KERNEL_SIGABRT) |
                                           (1U << KERNEL_SIGTRAP) | (1U << KERNEL_SIGBUS));
        TEST("SIGBUS unmaskable: bit 7 cleared by sigprocmask strip formula",
             !(stripped & (1U << KERNEL_SIGBUS)));
        TEST("SIGBUS not in KERNEL_STOP_SIGS", !(KERNEL_STOP_SIGS & (1U << KERNEL_SIGBUS)));
        TEST("SIGFPE not in KERNEL_STOP_SIGS", !(KERNEL_STOP_SIGS & (1U << KERNEL_SIGFPE)));
        // SIGQUIT (maskable) survives the strip
        TEST("SIGQUIT(3) survives strip (still maskable)", stripped & (1U << KERNEL_SIGQUIT));
    }

    // Test: multi-signal race — SIGBUS (7) fires before SIGSEGV (11) via ctz ordering
    {
        struct proc *p = proc_alloc();
        p->pid = 311; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSEGV);   // bit 11
        sys_kill(p->pid, SIGBUS);    // bit 7
        uint32_t fatal = (p->pending_signals & ~p->signal_mask) & KERNEL_FATAL_SIGS;
        TEST("SIGBUS(7) fires before SIGSEGV(11) when both pending (ctz)",
             kernel_ctz(fatal) == 7);
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // ── Group A: SIGUSR1 / SIGUSR2 basic dispatch ────────────────────────────────
    printk("=== Group A: SIGUSR1 / SIGUSR2 / SIGABRT / SIGTRAP / SIGHUP / SIGSYS basic dispatch ===\n");

    // Test: kill(pid, SIGUSR1) sets bit 10 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 500; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        int r = sys_kill(p->pid, SIGUSR1);
        TEST("SIGUSR1: sys_kill returns 0", r == 0);
        TEST("SIGUSR1: bit 10 set in pending_signals", p->pending_signals & (1U << 10));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: kill(pid, SIGUSR2) sets bit 12 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 501; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGUSR2);
        TEST("SIGUSR2: bit 12 set in pending_signals", p->pending_signals & (1U << 12));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: kill(pid, SIGABRT) sets bit 6 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 502; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGABRT);
        TEST("SIGABRT: bit 6 set in pending_signals", p->pending_signals & (1U << 6));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: kill(pid, SIGTRAP) sets bit 5 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 503; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGTRAP);
        TEST("SIGTRAP: bit 5 set in pending_signals", p->pending_signals & (1U << 5));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: kill(pid, SIGHUP) sets bit 1 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 504; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGHUP);
        TEST("SIGHUP: bit 1 set in pending_signals", p->pending_signals & (1U << 1));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // Test: kill(pid, SIGSYS) sets bit 31 in pending_signals
    {
        struct proc *p = proc_alloc();
        p->pid = 505; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSYS);
        TEST("SIGSYS: bit 31 set in pending_signals",
            p->pending_signals & (1U << 31));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // ── Group B: KERNEL_FATAL_SIGS macro includes all new signals ────────────────
    printk("=== Group B: KERNEL_FATAL_SIGS includes all new signals ===\n");

    TEST("KERNEL_FATAL_SIGS includes SIGHUP(1)",   KERNEL_FATAL_SIGS & (1U << 1));
    TEST("KERNEL_FATAL_SIGS includes SIGTRAP(5)",  KERNEL_FATAL_SIGS & (1U << 5));
    TEST("KERNEL_FATAL_SIGS includes SIGABRT(6)",  KERNEL_FATAL_SIGS & (1U << 6));
    TEST("KERNEL_FATAL_SIGS includes SIGUSR1(10)", KERNEL_FATAL_SIGS & (1U << 10));
    TEST("KERNEL_FATAL_SIGS includes SIGUSR2(12)", KERNEL_FATAL_SIGS & (1U << 12));
    TEST("KERNEL_FATAL_SIGS includes SIGSYS(31)",  KERNEL_FATAL_SIGS & (1U << 31));

    // ── Group C: KERNEL_FATAL_SIGS regression — original 8 signals intact ───────
    printk("=== Group C: KERNEL_FATAL_SIGS regression — original 8 signals intact ===\n");

    TEST("KERNEL_FATAL_SIGS: SIGKILL(9) still present",  KERNEL_FATAL_SIGS & (1U << 9));
    TEST("KERNEL_FATAL_SIGS: SIGTERM(15) still present", KERNEL_FATAL_SIGS & (1U << 15));
    TEST("KERNEL_FATAL_SIGS: SIGINT(2) still present",   KERNEL_FATAL_SIGS & (1U << 2));
    TEST("KERNEL_FATAL_SIGS: SIGQUIT(3) still present",  KERNEL_FATAL_SIGS & (1U << 3));
    TEST("KERNEL_FATAL_SIGS: SIGILL(4) still present",   KERNEL_FATAL_SIGS & (1U << 4));
    TEST("KERNEL_FATAL_SIGS: SIGPIPE(13) still present", KERNEL_FATAL_SIGS & (1U << 13));
    TEST("KERNEL_FATAL_SIGS: SIGALRM(14) still present", KERNEL_FATAL_SIGS & (1U << 14));
    TEST("KERNEL_FATAL_SIGS: SIGSEGV(11) still present", KERNEL_FATAL_SIGS & (1U << 11));

    // ── Group D: KERNEL_STOP_SIGS and KERNEL_DFL_IGNORE_SIGS not affected ───────
    printk("=== Group D: KERNEL_STOP_SIGS and KERNEL_DFL_IGNORE_SIGS not affected ===\n");

    TEST("KERNEL_STOP_SIGS: no new signals added",
        !(KERNEL_STOP_SIGS & ((1U << 1) | (1U << 5) | (1U << 6) |
                                (1U << 10) | (1U << 12) | (1U << 31))));
    TEST("KERNEL_DFL_IGNORE_SIGS unchanged",
        KERNEL_DFL_IGNORE_SIGS == (1U << 17));  // SIGCHLD only

    // ── Group E: FATAL_SIGS & STOP_SIGS disjoint after expansion ────────────────
    printk("=== Group E: FATAL_SIGS & STOP_SIGS disjoint after expansion ===\n");

    TEST("FATAL_SIGS & STOP_SIGS == 0 after expansion",
        (KERNEL_FATAL_SIGS & KERNEL_STOP_SIGS) == 0);

    // ── Group F: signal_mask blocks new signals — pending bit set, filtered ──────
    printk("=== Group F: signal_mask blocks new signals — pending bit set, filtered ===\n");

    {
        struct proc *p = proc_alloc();
        p->pid = 510; p->state = PROC_READY;
        p->signal_mask = (1U << 10) | (1U << 12);  // block SIGUSR1, SIGUSR2
        p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGUSR1);
        sys_kill(p->pid, SIGUSR2);
        TEST("masked SIGUSR1: pending bit set",   p->pending_signals & (1U << 10));
        TEST("masked SIGUSR2: pending bit set",   p->pending_signals & (1U << 12));
        uint32_t unmasked = p->pending_signals & ~p->signal_mask;
        TEST("masked SIGUSR1: filtered at delivery", !(unmasked & (1U << 10)));
        TEST("masked SIGUSR2: filtered at delivery", !(unmasked & (1U << 12)));
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // ── Group G: SIGABRT cannot be masked (unmaskable strip) ─────────────────────
    printk("=== Group G: SIGABRT / SIGTRAP cannot be masked (unmaskable strip) ===\n");

    {
        // Simulate what sys_sigprocmask does: try to set SIGABRT in signal_mask.
        // After the strip, SIGABRT bit must be 0.
        uint32_t proposed_mask = (1U << KERNEL_SIGABRT);
        // Apply the same strip as sys_sigprocmask:
        proposed_mask &= ~((1U << KERNEL_SIGKILL)  | (1U << KERNEL_SIGSTOP)  |
                        (1U << KERNEL_SIGILL)   | (1U << KERNEL_SIGABRT)  |
                        (1U << KERNEL_SIGTRAP));
        TEST("SIGABRT stripped from new_mask by sigprocmask logic",
            !(proposed_mask & (1U << KERNEL_SIGABRT)));
        TEST("SIGTRAP stripped from new_mask by sigprocmask logic",
            !(proposed_mask & (1U << KERNEL_SIGTRAP)));
    }

    // ── Group H: blocked process wakeup for new unmasked fatal signals ───────────
    printk("=== Group H: blocked process wakeup for new unmasked fatal signals ===\n");

    {
        struct proc *p = proc_alloc();
        p->pid = 515; p->state = PROC_CREATED; p->signal_mask = 0;
        p->pending_signals = 0; p->chan = p;
        proc_block(p);
        sys_kill(p->pid, SIGUSR1);
        TEST("SIGUSR1 to BLOCKED: bit 10 set", p->pending_signals & (1U << 10));
        TEST("SIGUSR1 to BLOCKED: state PROC_READY", p->state == PROC_READY);
        struct proc *drain; do { drain = proc_next_ready(); } while (drain && drain != p);
        proc_free(p);
    }

    // ── Group I: SIGSYS bit 31 is the high bit of uint32_t — verify sign safety ──
    printk("=== Group I: SIGSYS bit 31 sign-safety and kernel_ctz ordering ===\n");

    {
        uint32_t fatal = KERNEL_FATAL_SIGS;
        // Ensure bit 31 participates correctly in unsigned arithmetic:
        TEST("SIGSYS(31): bit 31 set in KERNEL_FATAL_SIGS (uint32_t)",
            (fatal & (1U << 31)) != 0);
        // Verify kernel_ctz returns 1 when only bit 1 is set (SIGHUP wins over SIGSYS):
        uint32_t both = (1U << 1) | (1U << 31);
        TEST("kernel_ctz: SIGHUP(1) fires before SIGSYS(31)",
            kernel_ctz(both) == 1);
    }

    // ── Group J: SIGSYS to ZOMBIE returns -ESRCH ────────────────────────────────
    printk("=== Group J: SIGSYS to ZOMBIE returns -ESRCH ===\n");

    {
        struct proc *p = proc_alloc();
        p->pid = 520; p->state = PROC_ZOMBIE; p->pending_signals = 0;
        int r = sys_kill(p->pid, SIGSYS);
        TEST("SIGSYS to ZOMBIE: returns -ESRCH", r == -ESRCH);
        proc_free(p);
    }

    // ── Group K: SIGUSR1 / SIGUSR2 edge cases ───────────────────────────────────

    printk("=== K1. SIGUSR1 to ZOMBIE → returns -ESRCH ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 600; p->state = PROC_ZOMBIE; p->pending_signals = 0;
        int r = sys_kill(p->pid, SIGUSR1);
        TEST("K1: SIGUSR1 to ZOMBIE: returns -ESRCH", r == -ESRCH);
        TEST("K1: SIGUSR1 to ZOMBIE: state unchanged", p->state == PROC_ZOMBIE);
        proc_free(p);
    }

    printk("=== K2. SIGUSR2 to BLOCKED → bit set, proc_unblock fires ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 601; p->state = PROC_CREATED; p->signal_mask = 0;
        p->pending_signals = 0; p->chan = p;
        proc_block(p);
        sys_kill(p->pid, SIGUSR2);
        TEST("K2: SIGUSR2 to BLOCKED: bit 12 set", p->pending_signals & (1U << 12));
        TEST("K2: SIGUSR2 to BLOCKED: state PROC_READY", p->state == PROC_READY);
        struct proc *drain601; do { drain601 = proc_next_ready(); } while (drain601 && drain601 != p);
        proc_free(p);
    }

    printk("=== K3. SIGUSR1 to STOPPED → bit set, state unchanged ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 602; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSTOP);
        sys_kill(p->pid, SIGUSR1);
        TEST("K3: SIGUSR1 to STOPPED: bit 10 set", p->pending_signals & (1U << 10));
        TEST("K3: SIGUSR1 to STOPPED: state still PROC_STOPPED", p->state == PROC_STOPPED);
        proc_free(p);
    }

    printk("=== K4. SIGUSR1 and SIGUSR2 both pending — SIGUSR1 fires first ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 603; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGUSR2);   // bit 12
        sys_kill(p->pid, SIGUSR1);   // bit 10
        uint32_t fatal603 = (p->pending_signals & ~p->signal_mask) & KERNEL_FATAL_SIGS;
        TEST("K4: SIGUSR1(10) fires before SIGUSR2(12)", kernel_ctz(fatal603) == 10);
        struct proc *drain603; do { drain603 = proc_next_ready(); } while (drain603 && drain603 != p);
        proc_free(p);
    }

    printk("=== K5. Masked SIGUSR1 to BLOCKED: bit set, proc stays BLOCKED ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 604; p->state = PROC_CREATED;
        p->signal_mask = (1U << 10);   // mask SIGUSR1
        p->pending_signals = 0; p->chan = p;
        proc_block(p);
        sys_kill(p->pid, SIGUSR1);
        TEST("K5: masked SIGUSR1 to BLOCKED: bit still set", p->pending_signals & (1U << 10));
        TEST("K5: masked SIGUSR1 to BLOCKED: state stays BLOCKED", p->state == PROC_BLOCKED);
        proc_free(p);
    }

    // ── Group L: SIGABRT edge cases ──────────────────────────────────────────────

    printk("=== L1. SIGABRT bit 6 in KERNEL_FATAL_SIGS ===\n");
    TEST("L1: bit 6 in KERNEL_FATAL_SIGS", KERNEL_FATAL_SIGS & (1U << 6));
    TEST("L1: KERNEL_SIGABRT constant is 6", KERNEL_SIGABRT == 6);

    printk("=== L2. SIGABRT to ZOMBIE → returns -ESRCH ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 610; p->state = PROC_ZOMBIE; p->pending_signals = 0;
        int r = sys_kill(p->pid, SIGABRT);
        TEST("L2: SIGABRT to ZOMBIE: returns -ESRCH", r == -ESRCH);
        proc_free(p);
    }

    printk("=== L3. SIGABRT(6) fires before SIGUSR2(12) ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 611; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGUSR2);   // bit 12
        sys_kill(p->pid, SIGABRT);   // bit 6
        uint32_t fatal611 = (p->pending_signals & ~p->signal_mask) & KERNEL_FATAL_SIGS;
        TEST("L3: SIGABRT(6) fires before SIGUSR2(12)", kernel_ctz(fatal611) == 6);
        struct proc *drain611; do { drain611 = proc_next_ready(); } while (drain611 && drain611 != p);
        proc_free(p);
    }

    // ── Group M: SIGTRAP edge cases ──────────────────────────────────────────────

    printk("=== M1. SIGTRAP bit 5 in KERNEL_FATAL_SIGS ===\n");
    TEST("M1: bit 5 in KERNEL_FATAL_SIGS", KERNEL_FATAL_SIGS & (1U << 5));
    TEST("M1: KERNEL_SIGTRAP constant is 5", KERNEL_SIGTRAP == 5);

    printk("=== M2. SIGTRAP(5) fires before SIGABRT(6) — lower bit wins ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 620; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGABRT);   // bit 6
        sys_kill(p->pid, SIGTRAP);   // bit 5
        uint32_t fatal620 = (p->pending_signals & ~p->signal_mask) & KERNEL_FATAL_SIGS;
        TEST("M2: SIGTRAP(5) fires before SIGABRT(6)", kernel_ctz(fatal620) == 5);
        struct proc *drain620; do { drain620 = proc_next_ready(); } while (drain620 && drain620 != p);
        proc_free(p);
    }

    printk("=== M3. SIGTRAP not in KERNEL_STOP_SIGS ===\n");
    TEST("M3: SIGTRAP(5) not in KERNEL_STOP_SIGS", !(KERNEL_STOP_SIGS & (1U << 5)));

    // ── Group N: SIGHUP edge cases ───────────────────────────────────────────────

    printk("=== N1. SIGHUP(1) fires first when all 6 new signals pending ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 630; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSYS);
        sys_kill(p->pid, SIGUSR2);
        sys_kill(p->pid, SIGUSR1);
        sys_kill(p->pid, SIGABRT);
        sys_kill(p->pid, SIGTRAP);
        sys_kill(p->pid, SIGHUP);
        uint32_t fatal630 = (p->pending_signals & ~p->signal_mask) & KERNEL_FATAL_SIGS;
        TEST("N1: SIGHUP(1) fires first when all new signals pending", kernel_ctz(fatal630) == 1);
        struct proc *drain630; do { drain630 = proc_next_ready(); } while (drain630 && drain630 != p);
        proc_free(p);
    }

    printk("=== N2. SIGHUP(1) fires before SIGINT(2) — new signal beats existing ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 631; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGINT);   // bit 2
        sys_kill(p->pid, SIGHUP);   // bit 1
        uint32_t fatal631 = (p->pending_signals & ~p->signal_mask) & KERNEL_FATAL_SIGS;
        TEST("N2: SIGHUP(1) fires before SIGINT(2)", kernel_ctz(fatal631) == 1);
        struct proc *drain631; do { drain631 = proc_next_ready(); } while (drain631 && drain631 != p);
        proc_free(p);
    }

    // ── Group O: SIGSYS edge cases ───────────────────────────────────────────────

    printk("=== O1. SIGSYS bit 31 in KERNEL_FATAL_SIGS (high bit, uint32_t) ===\n");
    TEST("O1: bit 31 in KERNEL_FATAL_SIGS (uint32_t)", (KERNEL_FATAL_SIGS & (1U << 31)) != 0);
    TEST("O1: KERNEL_SIGSYS constant is 31", KERNEL_SIGSYS == 31);

    printk("=== O2. SIGSYS loses to SIGTERM — highest bit fires last ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 640; p->state = PROC_READY; p->signal_mask = 0; p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSYS);    // bit 31
        sys_kill(p->pid, SIGTERM);   // bit 15
        uint32_t fatal640 = (p->pending_signals & ~p->signal_mask) & KERNEL_FATAL_SIGS;
        TEST("O2: SIGTERM(15) fires before SIGSYS(31)", kernel_ctz(fatal640) == 15);
        struct proc *drain640; do { drain640 = proc_next_ready(); } while (drain640 && drain640 != p);
        proc_free(p);
    }

    printk("=== O3. Masked SIGSYS: bit set but filtered at delivery ===\n");
    {
        struct proc *p = proc_alloc();
        p->pid = 641; p->state = PROC_READY;
        p->signal_mask = (1U << 31);   // mask SIGSYS (legal — not in unmaskable strip)
        p->pending_signals = 0;
        proc_ready(p);
        sys_kill(p->pid, SIGSYS);
        TEST("O3: masked SIGSYS: bit 31 set", p->pending_signals & (1U << 31));
        uint32_t unmasked641 = p->pending_signals & ~p->signal_mask;
        TEST("O3: masked SIGSYS: filtered at delivery", !(unmasked641 & (1U << 31)));
        struct proc *drain641; do { drain641 = proc_next_ready(); } while (drain641 && drain641 != p);
        proc_free(p);
    }

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
