/*
 * jobctl_test.c — Kernel-level unit tests for job-control primitives.
 *
 * Runs in RUN_TESTS mode (before schedule()).  Verifies:
 *   1. SIGTSTP (20) is in KERNEL_STOP_SIGS and stops a READY process.
 *   2. SIGTSTP sets stop_unreported for WUNTRACED.
 *   3. SIGCONT on SIGTSTP-stopped process restores READY + sets cont_unreported.
 *   4. SIGTSTP is maskable (signal_mask blocks it, unlike SIGSTOP).
 *   5. sys_tcsetpgrp / sys_tcgetpgrp read/write foreground_pgid.
 *   6. SIGTSTP to process group via kill(-pgid) stops all members.
 */

#include "trap/syscall.h"
#include "process/process.h"
#include "kernel/printk.h"
#include "tests/test.h"
#include "tty/term.h"
#include "signal.h"

static int passed = 0;
static int failed = 0;

void jobctl_test_run(void) {
    printk("\n========================================\n");
    printk("       Job Control Kernel Tests\n");
    printk("========================================\n\n");

    struct proc *saved_current = current;

    /* ── 1. SIGTSTP is in KERNEL_STOP_SIGS ──────────────────────────────── */
    printk("=== 1. SIGTSTP in KERNEL_STOP_SIGS ===\n");
    TEST("KERNEL_STOP_SIGS includes SIGTSTP (20)",
         KERNEL_STOP_SIGS & (1U << 20));
    TEST("KERNEL_STOP_SIGS includes SIGTTIN (21)",
         KERNEL_STOP_SIGS & (1U << 21));
    TEST("KERNEL_STOP_SIGS includes SIGTTOU (22)",
         KERNEL_STOP_SIGS & (1U << 22));
    TEST("KERNEL_STOP_SIGS excludes SIGKILL (9)",
         !(KERNEL_STOP_SIGS & (1U << 9)));

    /* ── 2. SIGTSTP stops a READY process + sets stop_unreported ─────────── */
    printk("=== 2. SIGTSTP stops READY proc ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("proc_alloc for SIGTSTP test", p != NULL);
        if (p) {
            p->pid = 400;
            p->state = PROC_READY;
            p->signal_mask = 0;
            p->pending_signals = 0;
            p->stop_unreported = 0;
            proc_ready(p);

            sys_kill(p->pid, 20); /* SIGTSTP */
            TEST("SIGTSTP: proc becomes PROC_STOPPED",
                 p->state == PROC_STOPPED);
            TEST("SIGTSTP: stop_unreported set",
                 p->stop_unreported == 1);

            proc_free(p);
        }
    }

    /* ── 3. SIGCONT on SIGTSTP-stopped proc ─────────────────────────────── */
    printk("=== 3. SIGCONT resumes SIGTSTP-stopped proc ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("proc_alloc for SIGCONT test", p != NULL);
        if (p) {
            p->pid = 401;
            p->state = PROC_READY;
            p->signal_mask = 0;
            p->pending_signals = 0;
            p->stop_unreported = 0;
            p->cont_unreported = 0;
            proc_ready(p);

            sys_kill(p->pid, 20); /* SIGTSTP → stop */
            TEST("SIGCONT: proc is stopped first",
                 p->state == PROC_STOPPED);

            sys_kill(p->pid, SIGCONT); /* resume */
            TEST("SIGCONT: proc becomes PROC_READY",
                 p->state == PROC_READY);
            TEST("SIGCONT: cont_unreported set",
                 p->cont_unreported == 1);

            proc_free(p);
        }
    }

    /* ── 4. SIGTSTP is maskable ─────────────────────────────────────────── */
    printk("=== 4. SIGTSTP maskable ===\n");
    {
        struct proc *p = proc_alloc();
        TEST("proc_alloc for mask test", p != NULL);
        if (p) {
            p->pid = 402;
            p->state = PROC_READY;
            p->signal_mask = (1U << 20); /* mask SIGTSTP */
            p->pending_signals = 0;
            proc_ready(p);

            sys_kill(p->pid, 20); /* SIGTSTP — masked */
            /* Signal should be pending but proc should NOT be stopped */
            TEST("SIGTSTP masked: pending bit set",
                 p->pending_signals & (1U << 20));
            /* Proc should remain READY because signal is masked */
            TEST("SIGTSTP masked: proc still READY (not stopped)",
                 p->state == PROC_READY);

            proc_free(p);
        }
    }

    /* ── 5. sys_tcsetpgrp / sys_tcgetpgrp ───────────────────────────────── */
    printk("=== 5. tcsetpgrp / tcgetpgrp ===\n");
    {
        extern terminal_t console_term;
        pid_t saved_fg = console_term.foreground_pgid;

        struct proc *p = proc_alloc();
        TEST("proc_alloc for tcsetpgrp test", p != NULL);
        if (p) {
            p->pid = 403;
            current = p;

            sys_tcsetpgrp(42);
            pid_t got = sys_tcgetpgrp();
            TEST("tcsetpgrp(42): tcgetpgrp returns 42", got == 42);

            sys_tcsetpgrp(0);
            got = sys_tcgetpgrp();
            TEST("tcsetpgrp(0): tcgetpgrp returns 0", got == 0);

            current = saved_current;
            proc_free(p);
        }

        /* Restore */
        console_term.foreground_pgid = saved_fg;
    }

    /* ── 6. SIGTSTP to process group stops all members ──────────────────── */
    printk("=== 6. SIGTSTP group delivery ===\n");
    {
        /* Use a separate caller in a different group so proc_stop() never
         * hits the (p == current) branch and context-switches to the
         * scheduler (which isn't running in the test environment). */
        struct proc *caller = proc_alloc();
        struct proc *leader = proc_alloc();
        struct proc *m1 = proc_alloc();
        TEST("proc_alloc for group SIGTSTP test",
             caller != NULL && leader != NULL && m1 != NULL);

        if (caller && leader && m1) {
            caller->pid = 403;
            caller->pgid = 403;
            caller->sid = 403;
            caller->state = PROC_READY;
            caller->signal_mask = 0;
            caller->pending_signals = 0;

            leader->pid = 404;
            leader->pgid = 404;
            leader->sid = 404;
            leader->state = PROC_READY;
            leader->signal_mask = 0;
            leader->pending_signals = 0;
            leader->stop_unreported = 0;

            m1->pid = 405;
            m1->pgid = 404; /* same group as leader */
            m1->sid = 404;
            m1->state = PROC_READY;
            m1->signal_mask = 0;
            m1->pending_signals = 0;
            m1->stop_unreported = 0;

            proc_ready(leader);
            proc_ready(m1);

            current = caller;
            sys_kill(-404, 20); /* SIGTSTP to group */

            TEST("SIGTSTP group: leader stopped",
                 leader->state == PROC_STOPPED);
            TEST("SIGTSTP group: member stopped",
                 m1->state == PROC_STOPPED);
            TEST("SIGTSTP group: leader stop_unreported",
                 leader->stop_unreported == 1);
            TEST("SIGTSTP group: member stop_unreported",
                 m1->stop_unreported == 1);
        }

        current = saved_current;
        if (caller) proc_free(caller);
        if (leader) proc_free(leader);
        if (m1) proc_free(m1);
    }

    printk("\n========================================\n");
    printk("  Job Control: %d passed, %d failed\n", passed, failed);
    printk("========================================\n\n");
}
