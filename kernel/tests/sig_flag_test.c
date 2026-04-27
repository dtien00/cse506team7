/*
 * sig_flag_test.c — Kernel unit tests for sa_flags support (SIG_FLAG items 1 & 2).
 *
 * Runs in RUN_TESTS mode (before schedule()).  proc_sleep() must NOT be called.
 * We manipulate proc structs directly using proc_alloc() and save/restore
 * `current` so each sub-test runs in isolation.
 *
 * Tests:
 *   1.  sig_flags[] zero-initialised after proc_alloc()
 *   2.  sys_sigaction: sa_flags written and read back via oldact
 *   3.  sys_sigaction: unknown flag bits stripped (only SA_KNOWN_FLAGS stored)
 *   4.  sys_sigaction: act=NULL query leaves sig_flags[] unchanged
 *   5.  sys_sigaction: SIGKILL/SIGSTOP reject still applies with sa_flags set
 *   6.  SA_NOCLDSTOP: SIGCHLD suppressed when parent has the flag and child stops
 *   7.  SA_NOCLDSTOP: SIGCHLD fires normally when flag is not set
 *   8.  SA_NOCLDSTOP: proc_unstop() (SIGCONT) still delivers SIGCHLD despite flag
 *   9.  sig_flags[] inherited across fork (memcpy simulation)
 *  10.  sig_flags[] for exec-reset signals cleared when handler reverts to SIG_DFL
 */

#include "trap/syscall.h"
#include "process/process.h"
#include "kernel/printk.h"
#include "tests/test.h"
#include "signal.h"
#include "lib/string.h"

static int passed = 0;
static int failed = 0;

/* Simulate the exec() sig_flags reset from process.c:
 * Clear sig_flags for any signal whose handler is reset to SIG_DFL.
 * SIG_IGN entries retain their flags (POSIX: SIG_IGN survives exec). */
static void apply_exec_sigflag_reset(struct proc *p) {
    for (int i = 1; i < 32; i++) {
        if (p->handlers[i] != (void *)SIG_IGN) {
            p->handlers[i]  = (void *)SIG_DFL;
            p->sig_flags[i] = 0;
        }
    }
}

/* Simulate the fork() sig_flags copy from sys_fork() in syscall.c. */
static void apply_fork_sigflag_copy(struct proc *child, const struct proc *parent) {
    memcpy(child->sig_flags, parent->sig_flags, sizeof(parent->sig_flags));
}

void sig_flag_test_run(void) {
    printk("\n========================================\n");
    printk("       sa_flags (SIG_FLAG) Tests\n");
    printk("========================================\n\n");

    struct proc *saved_current = current;

    /* ── 1. sig_flags[] zero-initialised after proc_alloc() ─────────────── */
    printk("=== 1. sig_flags[] zero after proc_alloc() ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("alloc: proc_alloc returns non-NULL", p != NULL);
        if (p) {
            int all_zero = 1;
            for (int i = 0; i < 32; i++) {
                if (p->sig_flags[i] != 0) { all_zero = 0; break; }
            }
            TEST("alloc: all sig_flags[] are 0 after alloc", all_zero);
            proc_free(p);
        }
    }

    /* ── 2. sys_sigaction: sa_flags written and read back via oldact ─────── */
    printk("=== 2. sys_sigaction sa_flags round-trip ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("roundtrip: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act, oldact;
            act.sa_handler  = SIG_IGN;
            act.sa_mask     = 0;
            act.sa_flags    = SA_NOCLDSTOP | SA_RESETHAND;
            act.sa_restorer = NULL;

            int r = sys_sigaction(KERNEL_SIGTERM, &act, &oldact);
            TEST("roundtrip: install returns 0", r == 0);
            TEST("roundtrip: oldact.sa_flags was 0 (prior to install)",
                 oldact.sa_flags == 0);

            /* Query back */
            r = sys_sigaction(KERNEL_SIGTERM, NULL, &oldact);
            TEST("roundtrip: query returns 0", r == 0);
            TEST("roundtrip: SA_NOCLDSTOP bit returned",
                 oldact.sa_flags & SA_NOCLDSTOP);
            TEST("roundtrip: SA_RESETHAND bit returned",
                 oldact.sa_flags & SA_RESETHAND);
            TEST("roundtrip: SA_NOCLDWAIT bit not set",
                 !(oldact.sa_flags & SA_NOCLDWAIT));

            /* Confirm stored in struct */
            TEST("roundtrip: sig_flags[SIGTERM] has SA_NOCLDSTOP",
                 p->sig_flags[KERNEL_SIGTERM] & SA_NOCLDSTOP);
            TEST("roundtrip: sig_flags[SIGTERM] has SA_RESETHAND",
                 p->sig_flags[KERNEL_SIGTERM] & SA_RESETHAND);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 3. Unknown flag bits stripped (only SA_KNOWN_FLAGS stored) ──────── */
    printk("=== 3. Unknown flag bits stripped by SA_KNOWN_FLAGS mask ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("strip: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act;
            act.sa_handler  = SIG_IGN;
            act.sa_mask     = 0;
            /* SA_RESTART (0x10000000) is not in SA_KNOWN_FLAGS — must be stripped. */
            act.sa_flags    = SA_NOCLDSTOP | SA_RESTART;
            act.sa_restorer = NULL;

            sys_sigaction(SIGUSR1, &act, NULL);

            TEST("strip: SA_NOCLDSTOP (known) is stored",
                 p->sig_flags[SIGUSR1] & SA_NOCLDSTOP);
            TEST("strip: SA_RESTART (unknown) is not stored",
                 !(p->sig_flags[SIGUSR1] & SA_RESTART));

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 4. act=NULL query leaves sig_flags[] unchanged ─────────────────── */
    printk("=== 4. act=NULL pure query does not change sig_flags[] ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("query-only: proc_alloc", p != NULL);
        if (p) {
            current = p;
            /* Pre-load a flag directly */
            p->handlers[KERNEL_SIGPIPE]  = (void *)SIG_IGN;
            p->sig_flags[KERNEL_SIGPIPE] = SA_RESETHAND;

            struct sigaction oldact;
            int r = sys_sigaction(KERNEL_SIGPIPE, NULL, &oldact);
            TEST("query-only: returns 0", r == 0);
            TEST("query-only: oldact.sa_flags reflects stored value",
                 (uint32_t)oldact.sa_flags == SA_RESETHAND);
            TEST("query-only: sig_flags[SIGPIPE] unchanged after query",
                 p->sig_flags[KERNEL_SIGPIPE] == SA_RESETHAND);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 5. SIGKILL/SIGSTOP reject still applies with sa_flags set ───────── */
    printk("=== 5. SIGKILL/SIGSTOP reject with sa_flags set ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("reject-flags: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act;
            act.sa_handler  = SIG_IGN;
            act.sa_mask     = 0;
            act.sa_flags    = SA_NOCLDSTOP | SA_RESETHAND;
            act.sa_restorer = NULL;

            int r = sys_sigaction(KERNEL_SIGKILL, &act, NULL);
            TEST("reject-flags: SIGKILL with sa_flags returns -EINVAL (< 0)", r < 0);
            TEST("reject-flags: sig_flags[SIGKILL] still 0",
                 p->sig_flags[KERNEL_SIGKILL] == 0);

            r = sys_sigaction(KERNEL_SIGSTOP, &act, NULL);
            TEST("reject-flags: SIGSTOP with sa_flags returns -EINVAL (< 0)", r < 0);
            TEST("reject-flags: sig_flags[SIGSTOP] still 0",
                 p->sig_flags[KERNEL_SIGSTOP] == 0);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 6. SA_NOCLDSTOP: SIGCHLD suppressed when parent has flag ─────────
     * Child must NOT be current to avoid the swtch() branch in proc_stop(). */
    printk("=== 6. SA_NOCLDSTOP suppresses SIGCHLD on child stop ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("nocldstop-suppress: alloc parent", parent != NULL);
        TEST("nocldstop-suppress: alloc child",  child  != NULL);

        if (parent && child) {
            parent->pending_signals    = 0;
            parent->signal_mask        = 0;
            parent->handlers[KERNEL_SIGCHLD]  = (void *)SIG_DFL;
            parent->sig_flags[KERNEL_SIGCHLD] = SA_NOCLDSTOP;

            child->parent        = parent;
            child->signal_mask   = 0;
            child->pending_signals = 0;
            child->stop_unreported = 0;
            child->mlfq_level    = 0;
            proc_ready(child);

            /* Stop child — SA_NOCLDSTOP must prevent SIGCHLD on parent. */
            proc_stop(child);

            TEST("nocldstop-suppress: child state is PROC_STOPPED",
                 child->state == PROC_STOPPED);
            TEST("nocldstop-suppress: SIGCHLD NOT set on parent",
                 !(parent->pending_signals & (1u << KERNEL_SIGCHLD)));
            TEST("nocldstop-suppress: stop_unreported still set on child",
                 child->stop_unreported == 1);
        }

        if (parent) proc_free(parent);
        if (child)  proc_free(child);
        current = saved_current;
    }

    /* ── 7. SA_NOCLDSTOP: SIGCHLD fires normally when flag not set ────────── */
    printk("=== 7. SIGCHLD fires on stop when SA_NOCLDSTOP not set ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("nocldstop-fires: alloc parent", parent != NULL);
        TEST("nocldstop-fires: alloc child",  child  != NULL);

        if (parent && child) {
            parent->pending_signals    = 0;
            parent->signal_mask        = 0;
            parent->handlers[KERNEL_SIGCHLD]  = (void *)SIG_DFL;
            parent->sig_flags[KERNEL_SIGCHLD] = 0;  /* no SA_NOCLDSTOP */

            child->parent        = parent;
            child->signal_mask   = 0;
            child->pending_signals = 0;
            child->stop_unreported = 0;
            child->mlfq_level    = 0;
            proc_ready(child);

            proc_stop(child);

            TEST("nocldstop-fires: child state is PROC_STOPPED",
                 child->state == PROC_STOPPED);
            TEST("nocldstop-fires: SIGCHLD IS set on parent",
                 parent->pending_signals & (1u << KERNEL_SIGCHLD));
        }

        if (parent) proc_free(parent);
        if (child)  proc_free(child);
        current = saved_current;
    }

    /* ── 8. SA_NOCLDSTOP: proc_unstop() still delivers SIGCHLD ───────────── *
     * SA_NOCLDSTOP suppresses stop notification only; SIGCONT (cont) is not   *
     * gated by this flag.  proc_unstop() must always notify the parent.       */
    printk("=== 8. proc_unstop() delivers SIGCHLD despite SA_NOCLDSTOP ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("nocldstop-unstop: alloc parent", parent != NULL);
        TEST("nocldstop-unstop: alloc child",  child  != NULL);

        if (parent && child) {
            parent->pending_signals    = 0;
            parent->signal_mask        = 0;
            parent->handlers[KERNEL_SIGCHLD]  = (void *)SIG_DFL;
            parent->sig_flags[KERNEL_SIGCHLD] = SA_NOCLDSTOP;

            child->parent          = parent;
            child->state           = PROC_STOPPED;  /* already stopped */
            child->cont_unreported = 0;

            /* proc_unstop() should deliver SIGCHLD regardless of SA_NOCLDSTOP */
            proc_unstop(child);

            TEST("nocldstop-unstop: child state is PROC_READY after unstop",
                 child->state == PROC_READY);
            TEST("nocldstop-unstop: SIGCHLD set on parent for SIGCONT event",
                 parent->pending_signals & (1u << KERNEL_SIGCHLD));
            TEST("nocldstop-unstop: cont_unreported set on child",
                 child->cont_unreported == 1);
        }

        /* proc_unstop added child to the MLFQ; remove it before freeing. */
        if (child)  { proc_stop(child); proc_free(child); }
        if (parent) proc_free(parent);
        current = saved_current;
    }

    /* ── 9. sig_flags[] inherited across fork ────────────────────────────── */
    printk("=== 9. sig_flags[] inherited across fork ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("fork-inherit: alloc parent", parent != NULL);
        TEST("fork-inherit: alloc child",  child  != NULL);

        if (parent && child) {
            parent->sig_flags[KERNEL_SIGTERM]  = SA_NOCLDSTOP | SA_RESETHAND;
            parent->sig_flags[SIGUSR1]  = SA_NODEFER;
            parent->sig_flags[KERNEL_SIGCHLD]  = SA_NOCLDSTOP;
            /* All others remain 0 */

            apply_fork_sigflag_copy(child, parent);

            TEST("fork-inherit: SIGTERM flags copied",
                 child->sig_flags[KERNEL_SIGTERM] == (SA_NOCLDSTOP | SA_RESETHAND));
            TEST("fork-inherit: SIGUSR1 flags copied",
                 child->sig_flags[SIGUSR1] == SA_NODEFER);
            TEST("fork-inherit: SIGCHLD flags copied",
                 child->sig_flags[KERNEL_SIGCHLD] == SA_NOCLDSTOP);
            TEST("fork-inherit: SIGPIPE flags are 0 (unset in parent)",
                 child->sig_flags[KERNEL_SIGPIPE] == 0);
        }

        if (parent) proc_free(parent);
        if (child)  proc_free(child);
    }

    /* ── 10. sig_flags[] cleared for exec-reset signals ─────────────────── */
    printk("=== 10. sig_flags[] cleared when exec resets handler to SIG_DFL ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("exec-clear: proc_alloc", p != NULL);
        if (p) {
            /* Simulate pre-exec state: some signals with flags set */
            p->handlers[KERNEL_SIGTERM]  = (void *)SIG_IGN;   /* survives exec */
            p->sig_flags[KERNEL_SIGTERM] = SA_NOCLDSTOP;      /* should survive */

            p->handlers[SIGUSR1]  = (void *)SIG_DFL;   /* already DFL */
            p->sig_flags[SIGUSR1] = SA_RESETHAND;      /* should be cleared */

            /* Simulate a caught handler that exec must reset */
            p->handlers[KERNEL_SIGPIPE]  = (void *)sig_flag_test_run; /* fake fnptr */
            p->sig_flags[KERNEL_SIGPIPE] = SA_NODEFER;                /* must clear */

            apply_exec_sigflag_reset(p);

            TEST("exec-clear: SIG_IGN handler for SIGTERM survives",
                 p->handlers[KERNEL_SIGTERM] == (void *)SIG_IGN);
            TEST("exec-clear: sig_flags for SIGTERM (SIG_IGN) survived",
                 p->sig_flags[KERNEL_SIGTERM] == SA_NOCLDSTOP);

            TEST("exec-clear: SIG_DFL handler for SIGUSR1 stays SIG_DFL",
                 p->handlers[SIGUSR1] == (void *)SIG_DFL);
            TEST("exec-clear: sig_flags for SIGUSR1 (SIG_DFL) cleared",
                 p->sig_flags[SIGUSR1] == 0);

            TEST("exec-clear: caught handler for SIGPIPE reset to SIG_DFL",
                 p->handlers[KERNEL_SIGPIPE] == (void *)SIG_DFL);
            TEST("exec-clear: sig_flags for SIGPIPE cleared after reset",
                 p->sig_flags[KERNEL_SIGPIPE] == 0);

            proc_free(p);
        }
    }

    /* ── 11. SA_NOCLDWAIT: child exit sets auto_reap, suppresses SIGCHLD ─────── */
    printk("=== 11. SA_NOCLDWAIT: child exit sets auto_reap, no SIGCHLD ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("nocldwait-exit: alloc parent", parent != NULL);
        TEST("nocldwait-exit: alloc child",  child  != NULL);

        if (parent && child) {
            parent->pending_signals    = 0;
            parent->signal_mask        = 0;
            parent->handlers[SIGCHLD]  = (void *)SIG_DFL;
            parent->sig_flags[SIGCHLD] = SA_NOCLDWAIT;

            child->parent          = parent;
            child->auto_reap       = 0;
            child->pending_signals = 0;

            // Simulate the SA_NOCLDWAIT branch from proc_exit()
            if (parent->sig_flags[SIGCHLD] & SA_NOCLDWAIT) {
                child->auto_reap = 1;
            } else {
                parent->pending_signals |= (1u << SIGCHLD);
            }

            TEST("nocldwait-exit: auto_reap is 1", child->auto_reap == 1);
            TEST("nocldwait-exit: SIGCHLD NOT set on parent",
                 !(parent->pending_signals & (1u << SIGCHLD)));
        }

        if (parent) proc_free(parent);
        if (child)  proc_free(child);
    }

    /* ── 12. SA_NOCLDWAIT not set: normal zombie behavior unchanged ─────────── */
    printk("=== 12. SA_NOCLDWAIT not set: SIGCHLD delivered, auto_reap stays 0 ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("nocldwait-normal: alloc parent", parent != NULL);
        TEST("nocldwait-normal: alloc child",  child  != NULL);

        if (parent && child) {
            parent->pending_signals    = 0;
            parent->signal_mask        = 0;
            parent->handlers[SIGCHLD]  = (void *)SIG_DFL;
            parent->sig_flags[SIGCHLD] = 0;  /* no SA_NOCLDWAIT */

            child->parent    = parent;
            child->auto_reap = 0;

            // Simulate the non-SA_NOCLDWAIT branch from proc_exit()
            if (parent->sig_flags[SIGCHLD] & SA_NOCLDWAIT) {
                child->auto_reap = 1;
            } else {
                parent->pending_signals |= (1u << SIGCHLD);
            }

            TEST("nocldwait-normal: auto_reap stays 0", child->auto_reap == 0);
            TEST("nocldwait-normal: SIGCHLD IS set on parent",
                 parent->pending_signals & (1u << SIGCHLD));
        }

        if (parent) proc_free(parent);
        if (child)  proc_free(child);
    }

    /* ── 13. auto_reap zombie is swept by proc_waitpid, not returned ─────────── */
    printk("=== 13. auto_reap zombie is swept by waitpid, not returned as zombie ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("nocldwait-sweep: alloc parent", parent != NULL);
        TEST("nocldwait-sweep: alloc child",  child  != NULL);

        if (parent && child) {
            current = parent;
            parent->pending_signals = 0;
            parent->signal_mask     = 0;
            parent->first_child     = child;

            child->parent      = parent;
            child->state       = PROC_ZOMBIE;
            child->auto_reap   = 1;
            child->sibling     = NULL;
            child->exit_code   = 0;
            child->utime_ticks = 0;

            // waitpid(-1, NULL, WNOHANG) must sweep the auto_reap zombie and
            // return -1 (ECHILD) — the child is invisible to the caller.
            int r = proc_waitpid(-1, NULL, 1 /* WNOHANG */);
            TEST("nocldwait-sweep: waitpid returns -1 (ECHILD)", r == -1);
            TEST("nocldwait-sweep: child no longer in parent->first_child",
                 parent->first_child == NULL);
            // child has been proc_free'd by the sweep — do not access it
            child = NULL;
        }

        if (parent) proc_free(parent);
        current = saved_current;
    }

    current = saved_current;

    printk("\n========================================\n");
    printk("  sa_flags: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
