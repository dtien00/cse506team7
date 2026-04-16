/*
 * test_jobctl.c — Userspace tests for job-control primitives (BG_FG.md §4).
 *
 * Tests run as normal user processes and exercise the kernel's job-control
 * support end-to-end through real syscalls:
 *
 *   1. Background job: fork + setpgid, parent does not block, reaps later.
 *   2. SIGTSTP stops child (simulated Ctrl-Z).
 *   3. fg resume: SIGCONT + waitpid (foreground pattern).
 *   4. bg resume: SIGCONT + WNOHANG (background pattern).
 *   5. bg on already-running background job — no-op.
 *   6. WUNTRACED + WCONTINUED round-trip.
 *   7. SIGTSTP is maskable (unlike SIGSTOP).
 *   8. waitpid(-pgid) for pipeline process group.
 *   9. tcsetpgrp / tcgetpgrp syscall smoke test.
 *  10. Rapid stop/continue stress test (20 cycles).
 */

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "us_test_common.h"

void run_test_jobctl(us_test_group_t *group) {
    ut_group_begin(group, "jobctl");

    /* ── 1. Background job: fork + setpgid, parent reaps with WNOHANG ────── */
    {
        pid_t pid = fork();
        TEST(group, "bg-job: fork succeeded", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);
            sleep(2);
            _exit(0);
        }

        if (pid > 0) {
            /* Parent does not block — simulates shell & operator */
            int status;
            pid_t r = waitpid(pid, &status, WNOHANG);
            TEST(group, "bg-job: child not yet finished (WNOHANG)", r == 0);

            /* Wait for child to complete */
            sleep(3);
            r = waitpid(pid, &status, WNOHANG);
            TEST(group, "bg-job: child reaped after completion",
                 r == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── 2. SIGTSTP stops child (simulated Ctrl-Z) ──────────────────────── */
    {
        pid_t pid = fork();
        TEST(group, "sigtstp-stop: fork succeeded", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);
            for (;;) sleep(1);
            _exit(0);
        }

        if (pid > 0) {
            sleep(1);
            kill(-pid, SIGTSTP);

            int status;
            pid_t r = waitpid(pid, &status, WUNTRACED);
            TEST(group, "sigtstp-stop: WUNTRACED reported stopped",
                 r == pid && WIFSTOPPED(status));
            TEST(group, "sigtstp-stop: WSTOPSIG == 20 (SIGTSTP)",
                 WIFSTOPPED(status) && WSTOPSIG(status) == 20);

            /* Clean up */
            kill(-pid, SIGCONT);
            sleep(1);
            kill(-pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
    }

    /* ── 3. fg resume: SIGCONT + waitpid (foreground pattern) ────────────── */
    {
        pid_t pid = fork();
        TEST(group, "fg-resume: fork succeeded", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);
            sleep(3);
            _exit(42);
        }

        if (pid > 0) {
            sleep(1);
            kill(-pid, SIGTSTP);

            int status;
            waitpid(pid, &status, WUNTRACED);
            TEST(group, "fg-resume: child stopped", WIFSTOPPED(status));

            /* fg: send SIGCONT and wait foreground */
            kill(-pid, SIGCONT);
            waitpid(pid, &status, 0);
            TEST(group, "fg-resume: child exited with 42",
                 WIFEXITED(status) && WEXITSTATUS(status) == 42);
        }
    }

    /* ── 4. bg resume: SIGCONT + WNOHANG (background pattern) ───────────── */
    {
        pid_t pid = fork();
        TEST(group, "bg-resume: fork succeeded", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);
            for (;;) sleep(1);
            _exit(0);
        }

        if (pid > 0) {
            sleep(1);
            kill(-pid, SIGTSTP);

            int status;
            waitpid(pid, &status, WUNTRACED);
            TEST(group, "bg-resume: child stopped", WIFSTOPPED(status));

            /* bg: send SIGCONT, don't block */
            kill(-pid, SIGCONT);

            /* Child re-enters sleep loop; should still be alive */
            sleep(1);
            pid_t r = waitpid(pid, &status, WNOHANG);
            TEST(group, "bg-resume: child running after SIGCONT (WNOHANG)",
                 r == 0);

            /* Clean up */
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
    }

    /* ── 5. bg on already-running background job — no-op ────────────────── */
    {
        pid_t pid = fork();
        TEST(group, "bg-noop: fork succeeded", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);
            sleep(3);
            _exit(0);
        }

        if (pid > 0) {
            /* Job is running in background — SIGCONT should be harmless */
            sleep(1);
            kill(-pid, SIGCONT);
            sleep(1);

            int status;
            pid_t r = waitpid(pid, &status, WNOHANG);
            TEST(group, "bg-noop: child still running after harmless SIGCONT",
                 r == 0);

            /* Clean up */
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
    }

    /* ── 6. WUNTRACED + WCONTINUED round-trip ───────────────────────────── */
    {
        pid_t pid = fork();
        TEST(group, "wuntraced-wcont: fork succeeded", pid >= 0);

        if (pid == 0) {
            for (;;) sleep(1);
            _exit(0);
        }

        if (pid > 0) {
            sleep(1);

            /* Stop with SIGSTOP */
            kill(pid, SIGSTOP);
            int status = 0;
            pid_t r = waitpid(pid, &status, WUNTRACED);
            TEST(group, "wuntraced-wcont: WUNTRACED reports stopped",
                 r == pid && WIFSTOPPED(status));
            TEST(group, "wuntraced-wcont: WSTOPSIG == 19 (SIGSTOP)",
                 WIFSTOPPED(status) && WSTOPSIG(status) == 19);

            /* Continue */
            kill(pid, SIGCONT);
            sleep(1);
            r = waitpid(pid, &status, WCONTINUED | WNOHANG);
            TEST(group, "wuntraced-wcont: WCONTINUED detected",
                 r == pid && WIFCONTINUED(status));

            /* Clean up */
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
    }

    /* ── 7. SIGTSTP is maskable (unlike SIGSTOP) ────────────────────────── */
    {
        pid_t pid = fork();
        TEST(group, "sigtstp-mask: fork succeeded", pid >= 0);

        if (pid == 0) {
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, 20); /* SIGTSTP */
            sigprocmask(SIG_BLOCK, &mask, NULL);
            sleep(3);
            /* If we survive, SIGTSTP was blocked */
            _exit(0);
        }

        if (pid > 0) {
            sleep(1);
            kill(pid, 20); /* SIGTSTP — should be blocked */

            /* Child should still be running */
            int status;
            pid_t r = waitpid(pid, &status, WNOHANG);
            TEST(group, "sigtstp-mask: child still running (SIGTSTP blocked)",
                 r == 0);

            /* Wait for child to finish normally */
            waitpid(pid, &status, 0);
            TEST(group, "sigtstp-mask: child survived with exit 0",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* ── 8. waitpid(-pgid) for pipeline process group ───────────────────── */
    {
        pid_t pid1 = fork();
        TEST(group, "waitpid-pgid: fork1 succeeded", pid1 >= 0);

        if (pid1 == 0) {
            setpgid(0, 0);
            sleep(2);
            _exit(10);
        }

        pid_t pid2 = fork();
        TEST(group, "waitpid-pgid: fork2 succeeded", pid2 >= 0);

        if (pid2 == 0) {
            setpgid(0, pid1);
            sleep(1);
            _exit(20);
        }

        if (pid1 > 0 && pid2 > 0) {
            sleep(1); /* Let setpgid take effect */

            int status;
            int reaped = 0;

            /* Reap both members via group wait */
            for (int i = 0; i < 3; i++) {
                pid_t r = waitpid(-pid1, &status, 0);
                if (r < 0) break;
                reaped++;
            }

            TEST(group, "waitpid-pgid: reaped both pipeline members",
                 reaped == 2);
        } else {
            if (pid1 > 0) { kill(pid1, SIGKILL); waitpid(pid1, NULL, 0); }
            if (pid2 > 0) { kill(pid2, SIGKILL); waitpid(pid2, NULL, 0); }
        }
    }

    /* ── 9. tcsetpgrp / tcgetpgrp syscall smoke test ────────────────────── */
    {
        pid_t orig = tcgetpgrp(0);
        TEST(group, "tcgetpgrp: returns non-negative", orig >= 0);

        pid_t mypid = getpid();
        int r = tcsetpgrp(0, mypid);
        TEST(group, "tcsetpgrp: returns 0", r == 0);

        pid_t now = tcgetpgrp(0);
        TEST(group, "tcgetpgrp: reflects new value", now == mypid);

        /* Restore original */
        if (orig >= 0)
            tcsetpgrp(0, orig);
    }

    /* ── 10. Rapid stop/continue stress test (20 cycles) ────────────────── */
    {
        pid_t pid = fork();
        TEST(group, "stress: fork succeeded", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);
            volatile int x = 0;
            for (;;) x++;
            _exit(0);
        }

        if (pid > 0) {
            sleep(1);
            int ok = 1;

            for (int i = 0; i < 20; i++) {
                kill(pid, SIGSTOP);
                int status;
                pid_t r = waitpid(pid, &status, WUNTRACED);
                if (r != pid || !WIFSTOPPED(status)) {
                    ok = 0;
                    break;
                }
                kill(pid, SIGCONT);
                /* Small yield to let scheduler run */
                for (volatile int j = 0; j < 1000; j++);
            }

            TEST(group, "stress: 20 stop/continue cycles succeeded", ok);

            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
    }

    /* ── 11. bg+fg: sleep survives stop/continue (sys_sleep restarts) ──── */
    {
        pid_t pid = fork();
        TEST(group, "bg-fg-sleep: fork succeeded", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);
            sleep(5);
            _exit(99);
        }

        if (pid > 0) {
            sleep(1);

            /* Simulate Ctrl+Z: stop the child */
            kill(-pid, SIGTSTP);
            int status;
            pid_t r = waitpid(pid, &status, WUNTRACED);
            TEST(group, "bg-fg-sleep: child stopped", WIFSTOPPED(status));

            /* Simulate bg: resume in background */
            kill(-pid, SIGCONT);

            /* Child should still be sleeping (not exited early) */
            sleep(1);
            r = waitpid(pid, &status, WNOHANG);
            TEST(group, "bg-fg-sleep: child still running after bg",
                 r == 0);

            /* Simulate fg: wait for child in foreground */
            r = waitpid(pid, &status, 0);
            TEST(group, "bg-fg-sleep: child exited with 99",
                 r == pid && WIFEXITED(status) && WEXITSTATUS(status) == 99);
        }
    }

    ut_group_end(group);
}
