/*
 * test_sched.c — Userspace tests for time-slice preemption.
 *
 * These tests run inside the OS as normal user processes.  They exercise
 * the scheduler end-to-end through real timer interrupts:
 *
 *  1. Round-robin fairness: N CPU-bound children all complete.
 *     Before the fix, each child was killed the first time the timer fired
 *     on it; with the fix they are re-queued as READY and eventually finish.
 *
 *  2. Exit-code preservation: a child that burns CPU then exits with a
 *     specific code must propagate that code correctly through wait(),
 *     proving the process was never silently killed by the timer.
 */

#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* Number of parallel CPU-bound children for the fairness test. */
#define NCHILDREN 4

void run_test_sched(us_test_group_t *group) {
    ut_group_begin(group, "scheduler");

    /* ── 1. Round-robin fairness ───────────────────────────────────────────
     * Fork NCHILDREN processes that each spin through a tight loop and then
     * exit(0).  If the timer kills rather than preempts, at least one child
     * will disappear with a non-zero exit status.
     */
    {
        pid_t pids[NCHILDREN];
        int all_spawned = 1;

        for (int i = 0; i < NCHILDREN; i++) {
            pids[i] = fork();
            if (pids[i] < 0) { all_spawned = 0; break; }
            if (pids[i] == 0) {
                volatile long c = 0;
                while (c < 2000000L) c++;
                _exit(0);
            }
        }

        TEST(group, "round-robin: all children spawned", all_spawned);

        int all_exited_ok = 1;
        for (int i = 0; i < NCHILDREN; i++) {
            if (pids[i] <= 0) continue;
            int status = 0;
            waitpid(pids[i], &status, 0);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                all_exited_ok = 0;
        }
        TEST(group, "round-robin: all children exited normally (not killed by timer)",
             all_exited_ok);
    }

    /* ── 2. Exit-code preserved after preemption ───────────────────────────
     * A single child burns enough CPU to be preempted at least once, then
     * exits with a known code.  The parent must receive that exact code.
     */
    {
        pid_t pid = fork();
        TEST(group, "exit-code test: fork succeeded", pid >= 0);

        if (pid == 0) {
            volatile long c = 0;
            while (c < 1000000L) c++;
            _exit(42);
        } else if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "exit-code: child exited normally",   WIFEXITED(status));
            TEST(group, "exit-code: correct value after CPU burn",
                 WIFEXITED(status) && WEXITSTATUS(status) == 42);
        }
    }

    ut_group_end(group);
}
