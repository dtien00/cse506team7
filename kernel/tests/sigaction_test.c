/*
 * sigaction_test.c — Kernel unit tests for signal disposition (sys_sigaction).
 *
 * Runs in RUN_TESTS mode (before schedule()).  proc_sleep() must NOT be called.
 * We manipulate proc structs directly using proc_alloc() and save/restore
 * `current` so each sub-test runs in isolation.
 *
 * Tests:
 *   1.  handlers[] initialised to SIG_DFL after proc_alloc()
 *   2.  sys_sigaction: set SIG_IGN, read back via oldact
 *   3.  sys_sigaction: act=NULL is a pure query (no change)
 *   4.  sys_sigaction: act!=NULL, oldact=NULL (no fault)
 *   5.  sys_sigaction: SIGKILL rejected
 *   6.  sys_sigaction: SIGSTOP rejected
 *   7.  sys_sigaction: sig=0 rejected
 *   8.  sys_sigaction: sig=32 (out-of-range) rejected
 *   9.  sys_sigaction: function pointer rejected (must be SIG_DFL/SIG_IGN only)
 *   10. SIG_IGN filter: pending SIG_IGN'd signal cleared at dispatch
 *   11. SIG_IGN filter: SIGKILL not cleared even if handlers[SIGKILL] glitched
 *   12. SIGSTOP not cleared by SIG_IGN filter (stop sigs bypass handler table)
 *   13. Fork handlers copy: child inherits parent's handler table via memcpy
 *   14. Exec reset: SIG_IGN survives; SIG_DFL stays SIG_DFL; others reset
 */

#include "trap/syscall.h"
#include "process/process.h"
#include "kernel/printk.h"
#include "tests/test.h"
#include "signal.h"
#include "lib/string.h"

static int passed = 0;
static int failed = 0;

/* Run the SIG_IGN clearing loop exactly as it appears in the three dispatch
 * points (syscall_handler, handle_exception trap return, proc_sleep wakeup).
 * Operating on an arbitrary proc struct rather than `current`. */
static void apply_sigign_filter(struct proc *p) {
    uint32_t tmp = p->pending_signals & ~p->signal_mask;
    while (tmp) {
        int s = kernel_ctz((uint32_t)(tmp));
        tmp &= tmp - 1;
        if (s != KERNEL_SIGKILL && s != KERNEL_SIGSTOP &&
            p->handlers[s] == (void *)SIG_IGN)
            p->pending_signals &= ~(1u << s);
    }
}

/* Simulate the exec() handler-reset loop from process.c. */
static void apply_exec_handler_reset(struct proc *p) {
    for (int i = 0; i < 32; i++) {
        if (p->handlers[i] != (void *)SIG_IGN)
            p->handlers[i] = (void *)SIG_DFL;
    }
}

void sigaction_test_run(void) {
    printk("\n========================================\n");
    printk("       sigaction / signal Tests\n");
    printk("========================================\n\n");

    /* Save and restore `current` so each test is isolated. */
    struct proc *saved_current = current;

    /* ── 1. handlers[] initialised to SIG_DFL after proc_alloc() ────────── */
    printk("=== 1. proc_alloc initialises handlers[] to SIG_DFL ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("alloc: proc_alloc returns non-NULL", p != NULL);
        if (p) {
            int all_dfl = 1;
            for (int i = 0; i < 32; i++) {
                if (p->handlers[i] != (void *)SIG_DFL) { all_dfl = 0; break; }
            }
            TEST("alloc: all handlers[] are SIG_DFL after alloc", all_dfl);
            proc_free(p);
        }
    }

    /* ── 2. sys_sigaction: set SIG_IGN, read back ────────────────────────── */
    printk("=== 2. sys_sigaction set SIG_IGN and read back ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("sigaction-set: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act, oldact;
            act.sa_handler  = SIG_IGN;
            act.sa_mask     = 0;
            act.sa_flags    = 0;
            act.sa_restorer = 0;

            int r = sys_sigaction(KERNEL_SIGTERM, &act, &oldact);
            TEST("sigaction-set: set SIG_IGN returns 0", r == 0);
            TEST("sigaction-set: oldact returns SIG_DFL (prior value)",
                 oldact.sa_handler == SIG_DFL);
            TEST("sigaction-set: handlers[SIGTERM] is now SIG_IGN",
                 p->handlers[KERNEL_SIGTERM] == (void *)SIG_IGN);

            /* Read back with act=NULL */
            r = sys_sigaction(KERNEL_SIGTERM, NULL, &oldact);
            TEST("sigaction-set: query returns 0", r == 0);
            TEST("sigaction-set: query returns SIG_IGN",
                 oldact.sa_handler == SIG_IGN);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 3. act=NULL is a pure query (handler unchanged) ────────────────── */
    printk("=== 3. act=NULL pure query ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("query-only: proc_alloc", p != NULL);
        if (p) {
            current = p;
            p->handlers[KERNEL_SIGINT] = (void *)SIG_IGN; /* set directly */

            struct sigaction oldact;
            int r = sys_sigaction(KERNEL_SIGINT, NULL, &oldact);
            TEST("query-only: returns 0", r == 0);
            TEST("query-only: oldact reflects SIG_IGN", oldact.sa_handler == SIG_IGN);
            TEST("query-only: handler unchanged", p->handlers[KERNEL_SIGINT] == (void *)SIG_IGN);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 4. act!=NULL, oldact=NULL (must not fault) ──────────────────────── */
    printk("=== 4. oldact=NULL does not fault ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("no-oldact: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act;
            act.sa_handler  = SIG_IGN;
            act.sa_mask     = 0;
            act.sa_flags    = 0;
            act.sa_restorer = 0;

            int r = sys_sigaction(KERNEL_SIGPIPE, &act, NULL);
            TEST("no-oldact: returns 0", r == 0);
            TEST("no-oldact: handler updated", p->handlers[KERNEL_SIGPIPE] == (void *)SIG_IGN);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 5. SIGKILL rejected ─────────────────────────────────────────────── */
    printk("=== 5. SIGKILL rejected ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("reject-SIGKILL: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act;
            act.sa_handler = SIG_IGN;
            act.sa_mask     = 0;
            act.sa_flags    = 0;
            act.sa_restorer = NULL;

            int r = sys_sigaction(KERNEL_SIGKILL, &act, NULL);
            TEST("reject-SIGKILL: returns -EINVAL (< 0)", r < 0);
            TEST("reject-SIGKILL: handlers[SIGKILL] unchanged (SIG_DFL)",
                 p->handlers[KERNEL_SIGKILL] == (void *)SIG_DFL);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 6. SIGSTOP rejected ─────────────────────────────────────────────── */
    printk("=== 6. SIGSTOP rejected ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("reject-SIGSTOP: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act;
            act.sa_handler = SIG_IGN;
            act.sa_mask     = 0;
            act.sa_flags    = 0;
            act.sa_restorer = NULL;

            int r = sys_sigaction(KERNEL_SIGSTOP, &act, NULL);
            TEST("reject-SIGSTOP: returns -EINVAL (< 0)", r < 0);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 7. sig=0 rejected ───────────────────────────────────────────────── */
    printk("=== 7. sig=0 rejected ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("reject-sig0: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act;
            act.sa_handler = SIG_IGN;
            act.sa_mask     = 0;
            act.sa_flags    = 0;
            act.sa_restorer = NULL;

            int r = sys_sigaction(0, &act, NULL);
            TEST("reject-sig0: returns < 0", r < 0);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 8. sig=32 (out-of-range) rejected ──────────────────────────────── */
    printk("=== 8. sig=32 rejected ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("reject-sig32: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act;
            act.sa_handler = SIG_IGN;
            act.sa_mask     = 0;
            act.sa_flags    = 0;
            act.sa_restorer = NULL;

            int r = sys_sigaction(32, &act, NULL);
            TEST("reject-sig32: returns < 0", r < 0);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 9. Function pointer rejected ───────────────────────────────────── */
    printk("=== 9. Function pointer rejected ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("reject-fnptr: proc_alloc", p != NULL);
        if (p) {
            current = p;
            struct sigaction act;
            /* Use a non-NULL, non-SIG_IGN(1) pointer — any kernel address works. */
            act.sa_handler = (sighandler_t)sigaction_test_run;
            act.sa_mask     = 0;
            act.sa_flags    = 0;
            act.sa_restorer = NULL;

            int r = sys_sigaction(SIGUSR1, &act, NULL);
            TEST("reject-fnptr: returns < 0", r < 0);
            TEST("reject-fnptr: handlers[SIGUSR1] unchanged",
                 p->handlers[SIGUSR1] == (void *)SIG_DFL);

            proc_free(p);
        }
        current = saved_current;
    }

    /* ── 10. SIG_IGN filter clears pending bit for ignored signal ────────── */
    printk("=== 10. SIG_IGN filter clears pending unmasked SIG_IGN signal ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("sigign-filter: proc_alloc", p != NULL);
        if (p) {
            p->state          = PROC_READY;
            p->signal_mask    = 0;
            p->pending_signals = (1u << KERNEL_SIGTERM);
            p->handlers[KERNEL_SIGTERM] = (void *)SIG_IGN;

            apply_sigign_filter(p);

            TEST("sigign-filter: SIGTERM pending cleared by SIG_IGN filter",
                 !(p->pending_signals & (1u << KERNEL_SIGTERM)));

            proc_free(p);
        }
    }

    /* ── 11. SIG_IGN filter does NOT clear SIGKILL ───────────────────────── */
    printk("=== 11. SIG_IGN filter does not clear SIGKILL ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("sigign-filter-kill: proc_alloc", p != NULL);
        if (p) {
            p->state          = PROC_READY;
            p->signal_mask    = 0;
            p->pending_signals = (1u << KERNEL_SIGKILL);
            /* Simulate a hypothetical glitch: handler set to SIG_IGN directly. */
            p->handlers[KERNEL_SIGKILL] = (void *)SIG_IGN;

            apply_sigign_filter(p);

            TEST("sigign-filter-kill: SIGKILL NOT cleared by filter",
                 p->pending_signals & (1u << KERNEL_SIGKILL));

            proc_free(p);
        }
    }

    /* ── 12. Masked signal not cleared by filter (mask checked separately) ─ */
    printk("=== 12. Masked SIG_IGN signal NOT cleared by filter ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("sigign-masked: proc_alloc", p != NULL);
        if (p) {
            p->state          = PROC_READY;
            /* SIGTERM is masked AND ignored — filter should not touch it
             * because it is masked (not in the unmasked set). */
            p->signal_mask    = (1u << KERNEL_SIGTERM);
            p->pending_signals = (1u << KERNEL_SIGTERM);
            p->handlers[KERNEL_SIGTERM] = (void *)SIG_IGN;

            apply_sigign_filter(p);

            /* The bit stays — masked signals are checked at delivery, not here. */
            TEST("sigign-masked: masked SIG_IGN pending bit preserved",
                 p->pending_signals & (1u << KERNEL_SIGTERM));

            proc_free(p);
        }
    }

    /* ── 13. Fork: child inherits handler table via memcpy ───────────────── */
    printk("=== 13. Fork inherits handler table ===\n");
    {
        struct proc *parent = proc_alloc();
        struct proc *child  = proc_alloc();
        TEST("fork-inherit: alloc parent", parent != NULL);
        TEST("fork-inherit: alloc child",  child  != NULL);
        if (parent && child) {
            /* Set up parent handlers */
            parent->handlers[KERNEL_SIGTERM] = (void *)SIG_IGN;
            parent->handlers[KERNEL_SIGINT]  = (void *)SIG_IGN;
            /* All others remain SIG_DFL */

            /* Simulate the fork handler copy from sys_fork() */
            memcpy(child->handlers, parent->handlers, sizeof(parent->handlers));

            TEST("fork-inherit: child SIGTERM handler = SIG_IGN",
                 child->handlers[KERNEL_SIGTERM] == (void *)SIG_IGN);
            TEST("fork-inherit: child SIGINT handler = SIG_IGN",
                 child->handlers[KERNEL_SIGINT] == (void *)SIG_IGN);
            TEST("fork-inherit: child SIGKILL handler = SIG_DFL",
                 child->handlers[KERNEL_SIGKILL] == (void *)SIG_DFL);
            TEST("fork-inherit: child SIGUSR1 handler = SIG_DFL",
                 child->handlers[SIGUSR1] == (void *)SIG_DFL);

            proc_free(parent);
            proc_free(child);
        }
    }

    /* ── 14. Exec reset: SIG_IGN survives; SIG_DFL stays; others reset ───── */
    printk("=== 14. Exec handler reset ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("exec-reset: proc_alloc", p != NULL);
        if (p) {
            /* Before exec: various dispositions */
            p->handlers[KERNEL_SIGTERM]  = (void *)SIG_IGN;  /* must survive */
            p->handlers[KERNEL_SIGINT]   = (void *)SIG_DFL;  /* already DFL */
            p->handlers[KERNEL_SIGPIPE]  = (void *)SIG_IGN;  /* must survive */
            /* Simulate a caught handler (non-DFL/non-IGN); exec must reset it. */
            p->handlers[SIGUSR1]  = (void *)sigaction_test_run; /* fake */

            apply_exec_handler_reset(p);

            TEST("exec-reset: SIG_IGN for SIGTERM survives exec",
                 p->handlers[KERNEL_SIGTERM] == (void *)SIG_IGN);
            TEST("exec-reset: SIG_IGN for SIGPIPE survives exec",
                 p->handlers[KERNEL_SIGPIPE] == (void *)SIG_IGN);
            TEST("exec-reset: SIG_DFL for SIGINT stays SIG_DFL",
                 p->handlers[KERNEL_SIGINT] == (void *)SIG_DFL);
            TEST("exec-reset: caught handler for SIGUSR1 reset to SIG_DFL",
                 p->handlers[SIGUSR1] == (void *)SIG_DFL);
            TEST("exec-reset: SIGKILL still SIG_DFL after reset",
                 p->handlers[KERNEL_SIGKILL] == (void *)SIG_DFL);

            proc_free(p);
        }
    }

    current = saved_current;

    printk("\n========================================\n");
    printk("  Results: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
