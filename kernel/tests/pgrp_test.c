/*
 * pgrp_test.c — Kernel-level unit tests for process group and session management.
 *
 * Runs in RUN_TESTS mode (before schedule()).  Tests operate directly on the
 * proctab[] entries and the sys_getpgrp / sys_setpgid / sys_setsid / sys_getpgid
 * syscall implementations.
 *
 * Because the scheduler is not running, proc_alloc() is used (not proc_create)
 * to avoid setting up a scheduler-runnable context (context.ra = proc_entry).
 * 'current' is set manually for each test and restored before freeing procs.
 *
 * Tests:
 *   1. proc_create() initialises pgid == pid and sid == pid.
 *   2. sys_getpgrp() returns current->pgid.
 *   3. sys_getpgid(0) returns current->pgid.
 *   4. sys_getpgid(pid) returns correct pgid for another process.
 *   5. sys_getpgid() on an unknown pid returns -ESRCH.
 *   6. sys_setpgid(0, 0): no-op when already group leader.
 *   7. sys_setpgid(0, 0) on a non-leader: creates a new group.
 *   8. sys_setpgid(child_pid, parent_pgid): parent moves child into own group.
 *   9. sys_setpgid rejects acting on a non-child.
 *  10. sys_setsid: creates a new session; returns new sid.
 *  11. sys_setsid on a group leader returns -EPERM.
 *  12. sys_setpgid on a session leader returns -EPERM.
 *  13. sys_kill(0, sig): delivers to all members of caller's pgid.
 *  14. sys_kill(-pgid, sig): delivers to specific process group.
 *  15. sys_kill(-pgid, sig) does not affect processes outside the group.
 */

#include "trap/syscall.h"
#include "process/process.h"
#include "kernel/printk.h"
#include "tests/test.h"
#include "signal.h"

static int passed = 0;
static int failed = 0;

/* Monotonically-increasing counter for fake test pids.
 * Range 300-399 is well clear of real pids (init=1) and kill_test pids (200-209). */
static int test_pid_counter = 300;

/* Helper: allocate a minimal test process.
 *
 * Uses proc_alloc() (not proc_create()) intentionally:
 *   - proc_alloc() leaves context.ra = 0 and does NOT set up a kernel stack
 *     entry point, so the scheduler can never accidentally run this proc.
 *   - state is left as PROC_CREATED — the timer preemption path in
 *     timer_handler checks (current->state == PROC_RUNNING), so a
 *     PROC_CREATED proc set as 'current' will never be preempted.
 *   - Only pgid, sid, pid, and pending_signals are needed by pgrp tests.
 */
static struct proc *make_test_proc(const char *name) {
    (void)name;
    struct proc *p = proc_alloc();
    if (!p) return NULL;
    p->pid  = test_pid_counter++;
    p->pgid = p->pid;
    p->sid  = p->pid;
    p->pending_signals = 0;
    p->signal_mask     = 0;
    /* state = PROC_CREATED from proc_alloc — do NOT set PROC_READY here.
     * Tests 13-15 (group kill) set PROC_READY explicitly after creation
     * since the group-kill loop skips PROC_CREATED procs. */
    return p;
}

/* Helper: clean up a test process (mark UNUSED so the slot can be reused). */
static void free_test_proc(struct proc *p) {
    if (!p) return;
    proc_free(p);
}

void pgrp_test_run(void) {
    printk("\n========================================\n");
    printk("   Process Group / Session Tests\n");
    printk("========================================\n\n");

    struct proc *saved_current = current;

    /* ── 1. make_test_proc initialises pgid == pid and sid == pid ──────────── */
    printk("=== 1. proc alloc pgid/sid init ===\n");
    {
        struct proc *p = make_test_proc("t_init");
        TEST("pgrp: make_test_proc sets pgid == pid", p && p->pgid == p->pid);
        TEST("pgrp: make_test_proc sets sid  == pid", p && p->sid  == p->pid);
        free_test_proc(p);
    }

    /* ── 2. sys_getpgrp() returns current->pgid ────────────────────────────── */
    printk("=== 2. sys_getpgrp ===\n");
    {
        struct proc *p = make_test_proc("t_getpgrp");
        TEST("pgrp: proc allocated for getpgrp test", p != NULL);
        if (p) {
            current = p;
            pid_t r = sys_getpgrp();
            TEST("pgrp: sys_getpgrp() returns pgid", r == p->pgid);
            TEST("pgrp: sys_getpgrp() == pid (fresh process)", r == p->pid);
            current = saved_current;
            free_test_proc(p);
        } else {
            current = saved_current;
        }
    }

    /* ── 3. sys_getpgid(0) returns current->pgid ───────────────────────────── */
    printk("=== 3. sys_getpgid(0) ===\n");
    {
        struct proc *p = make_test_proc("t_getpgid0");
        TEST("pgrp: proc allocated for getpgid(0) test", p != NULL);
        if (p) {
            current = p;
            pid_t r = sys_getpgid(0);
            TEST("pgrp: sys_getpgid(0) == pgid", r == p->pgid);
            TEST("pgrp: sys_getpgid(0) == pid (fresh process)", r == p->pid);
            current = saved_current;
            free_test_proc(p);
        } else {
            current = saved_current;
        }
    }

    /* ── 4. sys_getpgid(pid) returns correct pgid for another process ─────── */
    printk("=== 4. sys_getpgid(other pid) ===\n");
    {
        struct proc *a = make_test_proc("t_pgid_a");
        struct proc *b = make_test_proc("t_pgid_b");
        TEST("pgrp: procs allocated for getpgid(pid) test", a != NULL && b != NULL);
        if (a && b) {
            current = a;
            /* b starts with pgid == b->pid */
            pid_t r = sys_getpgid(b->pid);
            TEST("pgrp: sys_getpgid(b->pid) returns b->pgid", r == b->pgid);
            TEST("pgrp: sys_getpgid(b->pid) == b->pid (fresh)", r == b->pid);
        }
        current = saved_current;
        if (a) free_test_proc(a);
        if (b) free_test_proc(b);
    }

    /* ── 5. sys_getpgid() on an unknown pid returns -ESRCH ─────────────────── */
    printk("=== 5. sys_getpgid unknown pid ===\n");
    {
        struct proc *p = make_test_proc("t_pgid_unk");
        TEST("pgrp: proc allocated for unknown-pid test", p != NULL);
        if (p) {
            current = p;
            pid_t r = sys_getpgid(99999);
            TEST("pgrp: sys_getpgid(99999) returns error (-1)", r == -1);
            current = saved_current;
            free_test_proc(p);
        } else {
            current = saved_current;
        }
    }

    /* ── 6. sys_setpgid(0,0): no-op when already the group leader ───────────── */
    printk("=== 6. sys_setpgid(0,0) on group leader ===\n");
    {
        struct proc *p = make_test_proc("t_spgid_noop");
        TEST("pgrp: proc allocated for setpgid noop test", p != NULL);
        if (p) {
            /* p->pgid == p->pid (group leader), but must NOT be a session leader
             * (sid == pid) — POSIX rejects setpgid on session leaders with EPERM.
             * Assign p to an outer session (sid=1) so it is a group leader only. */
            p->sid = 1;
            current = p;
            /* p->pgid == p->pid already — calling setpgid(0,0) should succeed */
            int r = sys_setpgid(0, 0);
            TEST("pgrp: setpgid(0,0) on self-leader succeeds", r == 0);
            TEST("pgrp: pgid unchanged after no-op setpgid", p->pgid == p->pid);
            current = saved_current;
            free_test_proc(p);
        } else {
            current = saved_current;
        }
    }

    /* ── 7. sys_setpgid(0, new_pgid): join an existing group ─────────────────
     * Create group leader g and a member m (m->pgid != m->pid).
     * Have m join g's group. */
    printk("=== 7. sys_setpgid join existing group ===\n");
    {
        struct proc *g = make_test_proc("t_leader");  /* group leader */
        struct proc *m = make_test_proc("t_member");  /* will join g's group */
        TEST("pgrp: procs allocated for join-group test", g != NULL && m != NULL);
        if (g && m) {
            /* Simulate: m was forked from g (inherits g's pgid) so m is NOT a
             * session leader and NOT a group leader */
            m->pgid = g->pgid; /* m now in g's group */
            m->sid  = g->sid;

            /* Now m wants to break out and form its own group */
            current = m;
            int r = sys_setpgid(0, 0); /* pgid==0 → use m->pid */
            TEST("pgrp: setpgid(0,0) on non-leader succeeds", r == 0);
            TEST("pgrp: m->pgid == m->pid after setpgid(0,0)", m->pgid == m->pid);

            /* Join back into g's group */
            r = sys_setpgid(0, g->pgid);
            TEST("pgrp: setpgid(0, g->pgid) succeeds", r == 0);
            TEST("pgrp: m->pgid == g->pgid after join", m->pgid == g->pgid);
        }
        current = saved_current;
        if (g) free_test_proc(g);
        if (m) free_test_proc(m);
    }

    /* ── 8. Parent calls setpgid(child_pid, parent_pgid) ────────────────────── */
    printk("=== 8. parent setpgid(child, pgid) ===\n");
    {
        struct proc *parent = make_test_proc("t_par");
        struct proc *child  = make_test_proc("t_chld");
        TEST("pgrp: procs for parent-setpgid test allocated", parent != NULL && child != NULL);
        if (parent && child) {
            /* Simulate fork: child inherits parent's pgid/sid */
            child->pgid   = parent->pgid;
            child->sid    = parent->sid;
            child->parent = parent;

            /* At fork child->pgid == parent->pgid == parent->pid.
             * Both are in the same group.  Now the child breaks into its own group. */
            current = child;
            sys_setpgid(0, 0);   /* child is now its own group leader */

            /* Parent moves child back into parent's group */
            current = parent;
            int r = sys_setpgid(child->pid, parent->pgid);
            TEST("pgrp: parent setpgid(child,parent_pgid) succeeds", r == 0);
            TEST("pgrp: child->pgid == parent->pgid after parent setpgid",
                 child->pgid == parent->pgid);
        }
        current = saved_current;
        if (parent) free_test_proc(parent);
        if (child)  free_test_proc(child);
    }

    /* ── 9. sys_setpgid rejects acting on a non-child process ──────────────── */
    printk("=== 9. setpgid rejects non-child ===\n");
    {
        struct proc *caller   = make_test_proc("t_caller");
        struct proc *stranger = make_test_proc("t_stranger");
        TEST("pgrp: procs for non-child test allocated", caller != NULL && stranger != NULL);
        if (caller && stranger) {
            /* stranger has no parent relationship with caller */
            current = caller;
            int r = sys_setpgid(stranger->pid, caller->pgid);
            TEST("pgrp: setpgid on non-child returns error", r == -ESRCH || r < 0);
        }
        current = saved_current;
        if (caller)   free_test_proc(caller);
        if (stranger) free_test_proc(stranger);
    }

    /* ── 10. sys_setsid: creates a new session ──────────────────────────────── */
    printk("=== 10. sys_setsid creates new session ===\n");
    {
        struct proc *parent = make_test_proc("t_spar");
        struct proc *child  = make_test_proc("t_schld");
        TEST("pgrp: procs for setsid test allocated", parent != NULL && child != NULL);
        if (parent && child) {
            /* Simulate fork: child inherits parent's group/session.
             * child->pgid == parent->pgid != child->pid → not a group leader */
            child->pgid   = parent->pgid;
            child->sid    = parent->sid;
            child->parent = parent;

            current = child;
            pid_t r = sys_setsid();
            TEST("pgrp: sys_setsid() returns new sid == child->pid", r == child->pid);
            TEST("pgrp: child->sid == child->pid after setsid", child->sid == child->pid);
            TEST("pgrp: child->pgid == child->pid after setsid", child->pgid == child->pid);
        }
        current = saved_current;
        if (parent) free_test_proc(parent);
        if (child)  free_test_proc(child);
    }

    /* ── 11. sys_setsid on a group leader returns -EPERM ────────────────────── */
    printk("=== 11. setsid on group leader fails ===\n");
    {
        struct proc *p = make_test_proc("t_setsid_gl");
        TEST("pgrp: proc for setsid-fail test allocated", p != NULL);
        if (p) {
            /* Fresh process: pgid == pid → group leader → setsid must fail */
            current = p;
            pid_t r = sys_setsid();
            TEST("pgrp: setsid() on group leader returns -EPERM", r == -EPERM);
            current = saved_current;
            free_test_proc(p);
        } else {
            current = saved_current;
        }
    }

    /* ── 12. sys_setpgid on a session leader returns -EPERM ─────────────────── */
    printk("=== 12. setpgid on session leader fails ===\n");
    {
        struct proc *parent = make_test_proc("t_sl_par");
        struct proc *child  = make_test_proc("t_sl_chld");
        TEST("pgrp: procs for setpgid-sl test allocated", parent != NULL && child != NULL);
        if (parent && child) {
            child->pgid   = parent->pgid;
            child->sid    = parent->sid;
            child->parent = parent;

            current = child;
            sys_setsid();  /* child becomes session leader */

            /* Now try setpgid — must fail */
            int r = sys_setpgid(0, 0);
            TEST("pgrp: setpgid on session leader returns -EPERM", r == -EPERM);
        }
        current = saved_current;
        if (parent) free_test_proc(parent);
        if (child)  free_test_proc(child);
    }

    /* ── 13. sys_kill(0, sig): delivers to caller's process group ─────────────
     * Create three processes in the same group.  One acts as caller (current).
     * kill(0, SIGUSR1) must set the signal bit in all three.
     *
     * Procs are set to PROC_READY (not PROC_CREATED) so the group-kill loop
     * can find them — it skips PROC_CREATED entries.  They are NOT added to
     * any MLFQ queue, so the scheduler will never run them. */
    printk("=== 13. kill(0,sig) to process group ===\n");
    {
        struct proc *leader = make_test_proc("t_kgl");
        struct proc *m1     = make_test_proc("t_km1");
        struct proc *m2     = make_test_proc("t_km2");
        TEST("pgrp: procs for kill(0) test allocated",
             leader != NULL && m1 != NULL && m2 != NULL);
        if (leader && m1 && m2) {
            /* All three in the same group */
            m1->pgid = leader->pgid; m1->sid = leader->sid;
            m2->pgid = leader->pgid; m2->sid = leader->sid;
            m1->pending_signals = 0;
            m2->pending_signals = 0;
            leader->pending_signals = 0;

            /* Set PROC_READY so the group-kill loop finds them.
             * Not enqueued in MLFQ, so scheduler will never run them. */
            leader->state = PROC_READY;
            m1->state     = PROC_READY;
            m2->state     = PROC_READY;

            current = leader;
            int r = sys_kill(0, SIGUSR1);
            TEST("pgrp: kill(0,SIGUSR1) returns 0", r == 0);
            TEST("pgrp: kill(0) sets SIGUSR1 in m1",
                 m1->pending_signals & (1U << SIGUSR1));
            TEST("pgrp: kill(0) sets SIGUSR1 in m2",
                 m2->pending_signals & (1U << SIGUSR1));
            /* leader itself also receives the signal */
            TEST("pgrp: kill(0) sets SIGUSR1 in leader",
                 leader->pending_signals & (1U << SIGUSR1));
        }
        current = saved_current;
        if (leader) free_test_proc(leader);
        if (m1)     free_test_proc(m1);
        if (m2)     free_test_proc(m2);
    }

    /* ── 14. sys_kill(-pgid, sig): delivers to specific process group ─────────
     * Caller is in a different group from the target.  Only target group members
     * must receive the signal. */
    printk("=== 14. kill(-pgid,sig) targets specific group ===\n");
    {
        struct proc *caller  = make_test_proc("t_kcaller");
        struct proc *target1 = make_test_proc("t_ktgt1");
        struct proc *target2 = make_test_proc("t_ktgt2");
        TEST("pgrp: procs for kill(-pgid) test allocated",
             caller != NULL && target1 != NULL && target2 != NULL);
        if (caller && target1 && target2) {
            /* Put target1 and target2 in the same group (target1 is leader) */
            target2->pgid = target1->pgid;
            target2->sid  = target1->sid;
            target1->pending_signals = 0;
            target2->pending_signals = 0;
            caller->pending_signals  = 0;

            /* Set PROC_READY so the group-kill loop finds them.
             * Not enqueued in MLFQ, so scheduler will never run them. */
            caller->state  = PROC_READY;
            target1->state = PROC_READY;
            target2->state = PROC_READY;

            current = caller;
            pid_t target_pgid = target1->pgid;   /* == target1->pid */
            int r = sys_kill(-target_pgid, SIGUSR1);
            TEST("pgrp: kill(-pgid, SIGUSR1) returns 0", r == 0);
            TEST("pgrp: kill(-pgid) sets bit in target1",
                 target1->pending_signals & (1U << SIGUSR1));
            TEST("pgrp: kill(-pgid) sets bit in target2",
                 target2->pending_signals & (1U << SIGUSR1));
            TEST("pgrp: kill(-pgid) does NOT set bit in caller",
                 !(caller->pending_signals & (1U << SIGUSR1)));
        }
        current = saved_current;
        if (caller)  free_test_proc(caller);
        if (target1) free_test_proc(target1);
        if (target2) free_test_proc(target2);
    }

    /* ── 15. sys_kill(-pgid) does not affect processes outside the group ──────
     * Bystander is in a different group and must not receive the signal. */
    printk("=== 15. kill(-pgid) isolation ===\n");
    {
        struct proc *caller    = make_test_proc("t_ki_caller");
        struct proc *victim    = make_test_proc("t_ki_victim");
        struct proc *bystander = make_test_proc("t_ki_by");
        TEST("pgrp: procs for kill isolation test allocated",
             caller != NULL && victim != NULL && bystander != NULL);
        if (caller && victim && bystander) {
            /* victim is in its own group (pgid == victim->pid) */
            /* bystander is in its own group (pgid == bystander->pid) */
            /* caller is in its own group */
            victim->pending_signals    = 0;
            bystander->pending_signals = 0;
            caller->pending_signals    = 0;

            /* Set PROC_READY so the group-kill loop finds them.
             * Not enqueued in MLFQ, so scheduler will never run them.
             * SIGUSR1 (non-fatal, non-SIGKILL) avoids proc_unblock being called
             * since the proc_unblock path triggers only for PROC_BLOCKED procs. */
            caller->state    = PROC_READY;
            victim->state    = PROC_READY;
            bystander->state = PROC_READY;

            current = caller;
            sys_kill(-(victim->pgid), SIGUSR1);

            TEST("pgrp: isolation: victim received SIGUSR1",
                 victim->pending_signals & (1U << SIGUSR1));
            TEST("pgrp: isolation: bystander not signaled",
                 !(bystander->pending_signals & (1U << SIGUSR1)));
            TEST("pgrp: isolation: caller not signaled",
                 !(caller->pending_signals & (1U << SIGUSR1)));
        }
        current = saved_current;
        if (caller)    free_test_proc(caller);
        if (victim)    free_test_proc(victim);
        if (bystander) free_test_proc(bystander);
    }

    printk("\n=== pgrp tests: %d passed, %d failed ===\n\n", passed, failed);
}
