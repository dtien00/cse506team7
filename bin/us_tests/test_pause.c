#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "us_test_common.h"

void run_test_pause(us_test_group_t *group) {
    ut_group_begin(group, "pause");

    /* T1: pause() woken by SIGKILL from child — process exits, child observes */
    {
        pid_t pid = fork();
        TEST(group, "pause/sigkill: fork succeeded", pid >= 0);
        if (pid == 0) {
            /* child: pause and wait to be killed */
            pause();
            _exit(99); /* should never reach */
        } else {
            /* parent: give child time to reach pause(), then kill it */
            sleep(1);
            int r = kill(pid, 9);  /* SIGKILL */
            TEST(group, "pause/sigkill: kill(SIGKILL) returned 0", r == 0);
            int status = 0;
            waitpid(pid, &status, 0);
            /* Child died by signal (exit code -1 from proc_exit) */
            TEST(group, "pause/sigkill: child killed by signal", WIFSIGNALED(status));
        }
    }

    /* T2: pause() woken by SIGTERM from child */
    {
        pid_t pid = fork();
        TEST(group, "pause/sigterm: fork succeeded", pid >= 0);
        if (pid == 0) {
            /* Clear any inherited signal mask (e.g. test_signal blocks SIGTERM in
             * the parent and never restores it, so fork'd children inherit it). */
            sigset_t empty;
            sigemptyset(&empty);
            sigprocmask(SIG_SETMASK, &empty, NULL);
            pause();
            _exit(42);
        } else {
            sleep(1);
            kill(pid, 15);  /* SIGTERM */
            int status = 0;
            waitpid(pid, &status, 0);
            TEST(group, "pause/sigterm: child killed by signal", WIFSIGNALED(status));
        }
    }

    /* T3: Only targeted process is woken; sibling stays paused */
    {
        pid_t c1 = fork();
        if (c1 == 0) { pause(); _exit(0); }

        pid_t c2 = fork();
        if (c2 == 0) { pause(); _exit(0); }

        sleep(1);
        /* Kill only c1 */
        kill(c1, 9);
        int st1 = 0;
        pid_t got = waitpid(c1, &st1, 0);
        TEST(group, "pause/targeted: killed child reaped", got == c1 && WIFSIGNALED(st1));

        /* c2 should still be alive (paused) */
        pid_t got2 = waitpid(c2, NULL, 1 /*WNOHANG*/);
        TEST(group, "pause/targeted: sibling still paused", got2 == 0);

        /* Clean up */
        kill(c2, 9);
        waitpid(c2, NULL, 0);
    }

    /* T4: sched_yield() does NOT wake pause() — pause stays blocked */
    {
        pid_t pid = fork();
        TEST(group, "pause/no_yield_wake: fork succeeded", pid >= 0);
        if (pid == 0) {
            pause();
            _exit(77);
        } else {
            /* Parent yields many times — child must stay blocked */
            for (int i = 0; i < 20; i++) sched_yield();
            /* child should still be paused; WNOHANG returns 0 */
            int st = 0;
            pid_t r = waitpid(pid, &st, 1 /*WNOHANG*/);
            TEST(group, "pause/no_yield_wake: yield does not wake pause", r == 0);
            kill(pid, 9);
            waitpid(pid, NULL, 0);
        }
    }

    ut_group_end(group);
}
