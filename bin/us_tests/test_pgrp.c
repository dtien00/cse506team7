/*
 * test_pgrp.c — Userspace tests for process group and session management.
 *
 * Tests exercise getpgrp(), getpgid(), setpgid(), setsid(), and the process-
 * group kill variants kill(0, sig) and kill(-pgid, sig) end-to-end.
 *
 *  1. Initial state: every new process is its own group leader.
 *  2. getpgrp() / getpgid(0) agree.
 *  3. setpgid(0, 0): process joins a new group with its own pid as pgid.
 *  4. fork inherits pgid and sid from parent.
 *  5. setpgid from parent: place child in parent's group.
 *  6. setsid: child creates a new session; becomes group leader.
 *  7. setsid on a group leader fails with EPERM.
 *  8. kill(0, sig): sends to every member of the caller's process group.
 *  9. kill(-pgid, sig): kills a specific process group (pipeline kill pattern).
 * 10. kill(-pgid, sig) does NOT affect processes outside the target group.
 * 11. setpgid on a session leader is rejected (EPERM).
 * 12. getpgid on a non-existent pid returns an error.
 */

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "us_test_common.h"

void run_test_pgrp(us_test_group_t *group) {
    ut_group_begin(group, "pgrp");

    /* ── 1. Initial state: process is its own group leader ───────────────────
     * After proc_create(), pgid == pid.  getpgrp() must return getpid().
     */
    {
        TEST(group, "init: getpgrp() == getpid()",
             getpgrp() == getpid());
    }

    /* ── 2. getpgrp() and getpgid(0) are consistent ─────────────────────────
     */
    {
        TEST(group, "getpgid(0) == getpgrp()",
             getpgid(0) == getpgrp());
    }

    /* ── 3. setpgid(0, 0): explicit self-group creation ─────────────────────
     * A process that calls setpgid(0, 0) should have pgid == pid.
     * Since we start as our own group leader (from test 1), this is a no-op
     * that must still succeed.
     */
    {
        int r = setpgid(0, 0);
        TEST(group, "setpgid(0,0) succeeds (self is already group leader)", r == 0);
        TEST(group, "setpgid(0,0): getpgrp() still == getpid()",
             getpgrp() == getpid());
    }

    /* ── 4. fork inherits pgid and sid ───────────────────────────────────────
     * Child's getpgrp() must equal parent's getpgrp() right after fork.
     * Child exits with its own pgid as the exit code (capped to 8 bits).
     */
    {
        pid_t parent_pgid = getpgrp();
        pid_t pid = fork();
        TEST(group, "fork-inherit: fork succeeded", pid >= 0);

        if (pid == 0) {
            /* Report whether child pgid matches parent pgid */
            _exit(getpgrp() == parent_pgid ? 0 : 1);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "fork-inherit: child inherits parent pgid",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── 5. setpgid from parent: move child into parent's existing group ──────
     * Parent forks a child, then calls setpgid(child_pid, parent_pgid) before
     * the child has had a chance to exec.  Child should land in parent's group.
     */
    {
        pid_t parent_pgid = getpgrp();
        pid_t pid = fork();
        TEST(group, "setpgid-parent: fork succeeded", pid >= 0);

        if (pid == 0) {
            /* Child sleeps briefly so parent can call setpgid first */
            sleep(1);
            /* Report whether our pgid is now the parent's original pgid */
            _exit(getpgrp() == parent_pgid ? 0 : 1);
        }
        if (pid > 0) {
            /* Move child into our group (pid 0 == self, so use parent_pgid) */
            int r = setpgid(pid, parent_pgid);
            TEST(group, "setpgid-parent: setpgid(child, parent_pgid) succeeded", r == 0);
            TEST(group, "setpgid-parent: child is now in parent's group",
                 getpgid(pid) == parent_pgid);
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "setpgid-parent: child confirmed its own pgid",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── 6. setsid: child creates a new session ──────────────────────────────
     * A child that is NOT a process group leader may call setsid().
     * After setsid(): child->sid == child->pgid == child->pid.
     */
    {
        pid_t parent_pgid = getpgrp();
        pid_t pid = fork();
        TEST(group, "setsid: fork succeeded", pid >= 0);

        if (pid == 0) {
            /*
             * At fork the child inherits parent's pgid, so child->pgid != child->pid.
             * That means the child is NOT a group leader — setsid() must succeed.
             */
            pid_t r = setsid();
            if (r != getpid())  _exit(1); /* setsid must return new sid == our pid */
            if (getpgrp() != getpid()) _exit(2); /* pgid must equal pid */
            if (getpgid(0) != getpid()) _exit(3);
            _exit(0);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "setsid: child created new session",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
            (void)parent_pgid;
        }
    }

    /* ── 7. setsid on a group leader fails ───────────────────────────────────
     * The current process started as its own group leader (pgid == pid).
     * setsid() must return -1 (EPERM).
     */
    {
        /* Confirm we're our own group leader first */
        if (getpgrp() == getpid()) {
            pid_t r = setsid();
            TEST(group, "setsid: fails with EPERM when already group leader", r == -1);
        } else {
            SKIP(group, "setsid-eperm", "not a group leader in this context");
        }
    }

    /* ── 8. kill(0, sig): kills all members of the caller's process group ────
     * kill(0, SIGKILL) includes the caller, so we run the whole scenario inside
     * a forked wrapper that creates its own process group.  The wrapper forks
     * two children, verifies they share its pgid, then calls kill(0, SIGKILL).
     * All three die; the test runner (in a different group) survives and
     * confirms the wrapper was terminated by SIGKILL.
     */
    {
        pid_t wrapper = fork();
        TEST(group, "kill(0): fork for wrapper succeeded", wrapper >= 0);

        if (wrapper == 0) {
            /* New process group so kill(0) only hits this subtree */
            setpgid(0, 0);

            pid_t c1 = fork();
            if (c1 == 0) { for (;;) sleep(1); }
            pid_t c2 = fork();
            if (c2 == 0) { for (;;) sleep(1); }

            if (c1 > 0 && c2 > 0) {
                sleep(1);
                /* Verify children inherited our group before we die */
                if (getpgid(c1) != getpgrp() || getpgid(c2) != getpgrp())
                    _exit(1); /* pgid mismatch */
                kill(0, SIGKILL); /* kills wrapper + c1 + c2 */
            }
            _exit(99); /* should not reach here */
        }

        if (wrapper > 0) {
            int ws = 0;
            waitpid(wrapper, &ws, 0);
            TEST(group, "kill(0): wrapper killed by SIGKILL (group kill worked)",
                 WIFSIGNALED(ws) && WTERMSIG(ws) == SIGKILL);
        }
    }

    /* ── 9. kill(-pgid, sig): pipeline kill pattern ─────────────────────────
     * Parent forks two children and places them in a new process group together
     * (shell pipeline simulation).  kill(-group_pgid, SIGKILL) terminates both.
     */
    {
        pid_t pid1 = fork();
        if (pid1 == 0) {
            /* Become new group leader */
            setpgid(0, 0);
            for (;;) sleep(1);
        }

        /* Give pid1 a moment to call setpgid */
        sleep(1);

        pid_t pipeline_pgid = (pid1 > 0) ? pid1 : getpid();

        pid_t pid2 = fork();
        if (pid2 == 0) {
            /* Join pid1's process group */
            setpgid(0, pipeline_pgid);
            for (;;) sleep(1);
        }

        if (pid1 > 0 && pid2 > 0) {
            sleep(1); /* let pid2 join the group */

            TEST(group, "kill(-pgid): pid1 is its own group leader",
                 getpgid(pid1) == pid1);
            TEST(group, "kill(-pgid): pid2 joined pid1's group",
                 getpgid(pid2) == pid1);

            /* Kill the entire pipeline */
            int r = kill(-pipeline_pgid, SIGKILL);
            TEST(group, "kill(-pgid): kill call succeeded", r == 0);

            int s1 = 0, s2 = 0;
            waitpid(pid1, &s1, 0);
            waitpid(pid2, &s2, 0);
            TEST(group, "kill(-pgid): pid1 terminated by signal",
                 WIFSIGNALED(s1));
            TEST(group, "kill(-pgid): pid2 terminated by signal",
                 WIFSIGNALED(s2));
        }
    }

    /* ── 10. kill(-pgid) does NOT affect processes outside the target group ──
     * Fork a bystander in a different group.  Kill a different group.
     * Bystander must survive.
     */
    {
        /* Bystander: separate group */
        pid_t bystander = fork();
        if (bystander == 0) {
            setpgid(0, 0);  /* own group */
            sleep(3);       /* live long enough */
            _exit(42);
        }

        /* Target group: two short-lived victims */
        pid_t victim = fork();
        if (victim == 0) {
            setpgid(0, 0);      /* own group, different from bystander */
            for (;;) sleep(1);
        }

        if (bystander > 0 && victim > 0) {
            sleep(1);
            pid_t victim_pgid = getpgid(victim);
            pid_t bystander_pgid = getpgid(bystander);

            TEST(group, "kill(-pgid) isolation: groups are different",
                 victim_pgid != bystander_pgid);

            kill(-victim_pgid, SIGKILL);
            int vs = 0;
            waitpid(victim, &vs, 0);
            TEST(group, "kill(-pgid) isolation: victim killed", WIFSIGNALED(vs));

            /* Bystander should still be running; reap it */
            int bs = 0;
            pid_t bw = waitpid(bystander, &bs, 0);
            TEST(group, "kill(-pgid) isolation: bystander survived",
                 bw == bystander && WIFEXITED(bs) && WEXITSTATUS(bs) == 42);
        }
    }

    /* ── 11. setpgid on a session leader is rejected ─────────────────────────
     * Use a child that calls setsid() (making itself a session leader), then
     * tries setpgid() — must fail.
     */
    {
        pid_t pid = fork();
        if (pid == 0) {
            setsid();  /* become session leader (child pgid != child pid at fork) */
            int r = setpgid(0, 0);
            /* As a session leader pgid == pid, setpgid must return -1 */
            _exit(r == -1 ? 0 : 1);
        }
        if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "setpgid on session leader returns error",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── 12. getpgid on a non-existent pid returns an error ─────────────────
     */
    {
        pid_t r = getpgid(9999);
        TEST(group, "getpgid(9999) returns -1 (ESRCH)", r == -1);
    }

    ut_group_end(group);
}
