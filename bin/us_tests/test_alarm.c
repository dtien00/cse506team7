/*
 * test_alarm.c — Tests for alarm() syscall (TC-14 and edge cases)
 *
 * Covers:
 *   T1  Basic delivery   — alarm(2) kills a pausing child via SIGALRM
 *   T2  Timing window    — delivery happens within an acceptable tick range
 *   T3  Cancel           — alarm(0) cancels; process survives and exits normally
 *   T4  Return value     — first call returns 0; immediate second call returns
 *                          remaining seconds from the previous alarm
 *   T5  Override         — alarm(10) then alarm(2) fires at ~2 s, not ~10 s
 *   T6  alarm(0) return  — alarm(0) when no alarm is pending returns 0
 *
 * Timer resolution: TICKS_PER_SEC = 10 (0.1 s per tick).
 * A 2-second alarm spans 20 ticks; we allow 18–30 ticks to account for
 * scheduler jitter and MLFQ preemption.
 *
 * Requires: SIGALRM (14) in KERNEL_FATAL_SIGS so that the default action
 * terminates the process — consistent with POSIX "terminate" default.
 * When sigaction() is implemented (Section 1 item 2), that check will be
 * conditioned on SIG_DFL disposition instead.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "us_test_common.h"

/* TICKS_PER_SEC = 10 per kernel/include/trap/trap.h — not exposed to userspace */
#define ALARM_TICKS_PER_SEC 10

void run_test_alarm(us_test_group_t *group) {
    ut_group_begin(group, "alarm");

    /* ── T1 & T2: Basic delivery and timing ─────────────────────────────────
     * Fork a child that calls alarm(2) then pause().  The alarm must fire
     * ~2 s later, deliver SIGALRM, and terminate the child.
     * Parent checks WIFSIGNALED + WTERMSIG == SIGALRM and measures elapsed
     * ticks via uptime() to verify the alarm fired in the expected window.
     */
    {
        int before = (int)uptime();

        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "alarm/basic: fork succeeded", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "alarm/basic: fork succeeded", 1);
        }

        if (pid == 0) {
            alarm(2);
            pause();
            _exit(1); /* must not reach — SIGALRM should terminate us */
        } else if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            int elapsed = (int)uptime() - before;

            TEST(group, "alarm/basic: child terminated by signal",
                 WIFSIGNALED(status));
            TEST(group, "alarm/basic: terminating signal is SIGALRM",
                 WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM);
            /* 18 ticks = 1.8 s (lower bound), 30 ticks = 3.0 s (upper bound) */
            TEST(group, "alarm/basic: fired within 18-30 ticks of alarm(2)",
                 elapsed >= 18 && elapsed <= 30);
        } else {
            SKIP(group, "alarm/basic: child terminated by signal", "fork failed");
            SKIP(group, "alarm/basic: terminating signal is SIGALRM", "fork failed");
            SKIP(group, "alarm/basic: fired within 18-30 ticks of alarm(2)", "fork failed");
        }
    }

    /* ── T3: alarm(0) cancels a pending alarm ────────────────────────────────
     * Child sets a 2 s alarm then immediately cancels it with alarm(0).
     * It then sleeps 4 s and exits cleanly.  If the alarm had not been
     * cancelled it would have fired during sleep() and killed the child.
     */
    {
        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "alarm/cancel: fork succeeded", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "alarm/cancel: fork succeeded", 1);
        }

        if (pid == 0) {
            alarm(2);
            alarm(0);    /* cancel */
            sleep(4);    /* would be killed here if alarm were still live */
            _exit(0);
        } else if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);

            TEST(group, "alarm/cancel: child exited normally (not signaled)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
            TEST(group, "alarm/cancel: WIFSIGNALED is false",
                 !WIFSIGNALED(status));
        } else {
            SKIP(group, "alarm/cancel: child exited normally (not signaled)", "fork failed");
            SKIP(group, "alarm/cancel: WIFSIGNALED is false", "fork failed");
        }
    }

    /* ── T4: Return value semantics ──────────────────────────────────────────
     * POSIX: alarm(n) returns the number of seconds remaining from any
     * previously scheduled alarm, or 0 if none.
     *
     *   alarm(5)  → must return 0   (no previous alarm)
     *   alarm(0)  → must return 4-5 (called immediately after; ceiling
     *               division of ~50 remaining ticks / 10 ticks/s = 5, or 4
     *               if one tick elapsed between the two calls)
     *
     * The child encodes results into its exit code:
     *   0  = both return values were correct
     *   1  = first alarm() returned non-zero  (bug)
     *   2  = second alarm() returned out-of-range (< 4 or > 5)
     */
    {
        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "alarm/retval: fork succeeded", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "alarm/retval: fork succeeded", 1);
        }

        if (pid == 0) {
            unsigned int r0 = alarm(5);
            if (r0 != 0)
                _exit(1); /* first call must return 0 */

            unsigned int r1 = alarm(0); /* cancel; returns remaining seconds */
            if (r1 < 4 || r1 > 5)
                _exit(2); /* accept 4 or 5 (one-tick jitter from ceiling division) */

            _exit(0);
        } else if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);

            TEST(group, "alarm/retval: child exited (not signaled)",
                 WIFEXITED(status));
            TEST(group, "alarm/retval: first alarm() returned 0",
                 WIFEXITED(status) && WEXITSTATUS(status) != 1);
            TEST(group, "alarm/retval: second alarm() returned remaining seconds (4-5)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        } else {
            SKIP(group, "alarm/retval: child exited (not signaled)", "fork failed");
            SKIP(group, "alarm/retval: first alarm() returned 0", "fork failed");
            SKIP(group, "alarm/retval: second alarm() returned remaining seconds (4-5)", "fork failed");
        }
    }

    /* ── T5: Override — new alarm replaces old one ───────────────────────────
     * Child sets alarm(10) then immediately overrides it with alarm(2).
     * The child must be killed in ~2 s, not ~10 s.
     * Elapsed ticks must be in the 18-50 range (< 100 rules out the 10 s alarm).
     */
    {
        int before = (int)uptime();

        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "alarm/override: fork succeeded", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "alarm/override: fork succeeded", 1);
        }

        if (pid == 0) {
            alarm(10); /* would fire in 10 s */
            alarm(2);  /* overrides: now fires in 2 s */
            pause();
            _exit(1);  /* must not reach */
        } else if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);
            int elapsed = (int)uptime() - before;

            TEST(group, "alarm/override: child killed by SIGALRM (not 10 s alarm)",
                 WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM);
            TEST(group, "alarm/override: elapsed < 50 ticks (fired at ~2 s, not ~10 s)",
                 elapsed >= 18 && elapsed <= 50);
        } else {
            SKIP(group, "alarm/override: child killed by SIGALRM (not 10 s alarm)", "fork failed");
            SKIP(group, "alarm/override: elapsed < 50 ticks (fired at ~2 s, not ~10 s)", "fork failed");
        }
    }

    /* ── T6: alarm(0) with no pending alarm returns 0 ────────────────────────
     * alarm(0) when there is no scheduled alarm must return 0, not garbage.
     */
    {
        pid_t pid = fork();
        if (pid < 0) {
            SKIP(group, "alarm/zero-return: fork succeeded", "fork failed (resource exhaustion)");
        } else {
            TEST(group, "alarm/zero-return: fork succeeded", 1);
        }

        if (pid == 0) {
            unsigned int r = alarm(0); /* no prior alarm */
            _exit(r == 0 ? 0 : 1);
        } else if (pid > 0) {
            int status = 0;
            waitpid(pid, &status, 0);

            TEST(group, "alarm/zero-return: alarm(0) with no prior alarm returns 0",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        } else {
            SKIP(group, "alarm/zero-return: alarm(0) with no prior alarm returns 0", "fork failed");
        }
    }

    ut_group_end(group);
}
