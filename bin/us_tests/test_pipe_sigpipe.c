/*
 * test_pipe_sigpipe.c — Userspace test for SIGPIPE on broken pipe.
 *
 * POSIX requires that writing to a pipe whose read end is closed delivers
 * SIGPIPE to the writer *and* returns -1/EPIPE.  Tests:
 *
 *  1. Default disposition: child writes to broken pipe → killed by SIGPIPE.
 *  2. SIG_IGN disposition: child ignores SIGPIPE, write returns -1 (EPIPE).
 *  3. Pipe EOF: reader sees EOF (read returns 0) when all writers close.
 */

#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include "us_test_common.h"

void run_test_pipe_sigpipe(us_test_group_t *group) {
    ut_group_begin(group, "pipe_sigpipe");

    /* ── 1. SIGPIPE kills writer (default disposition) ──────────────────── */
    {
        int fds[2];
        int r = pipe(fds);
        TEST(group, "SIGPIPE-default: pipe created", r == 0);

        pid_t pid = fork();
        TEST(group, "SIGPIPE-default: fork succeeded", pid >= 0);

        if (pid == 0) {
            /* Child: close read end, then write → broken pipe */
            close(fds[0]);
            close(fds[1]);  /* close our write end so parent is sole writer */
            _exit(99);      /* should not reach here normally */
        }

        /* Parent: close read end so pipe is broken, then wait */
        close(fds[0]);

        /* Give child time to exit so pipe is set up */
        /* Actually, parent is the writer. Re-do: parent writes, child closes read */
        close(fds[1]);
        waitpid(pid, NULL, 0);

        /* Re-do this test properly with parent as writer */
        r = pipe(fds);
        pid = fork();
        if (pid == 0) {
            /* Child is the writer */
            close(fds[0]);  /* close read end */
            sleep(1);       /* let parent close read end */
            char buf[64];
            for (int i = 0; i < 64; i++) buf[i] = 'A';
            write(fds[1], buf, 64);  /* should trigger SIGPIPE → death */
            /* If we get here, SIGPIPE was not delivered */
            _exit(99);
        }

        /* Parent closes both ends — read end closure makes pipe broken */
        close(fds[0]);
        close(fds[1]);

        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "SIGPIPE-default: child killed by signal",
             WIFSIGNALED(status));
        TEST(group, "SIGPIPE-default: signal is SIGPIPE (13)",
             WIFSIGNALED(status) && WTERMSIG(status) == SIGPIPE);
    }

    /* ── 2. SIGPIPE ignored → write returns -1 ─────────────────────────── */
    {
        int fds[2];
        int r = pipe(fds);
        TEST(group, "SIGPIPE-ignored: pipe created", r == 0);

        pid_t pid = fork();
        TEST(group, "SIGPIPE-ignored: fork succeeded", pid >= 0);

        if (pid == 0) {
            /* Child: ignore SIGPIPE, then write to broken pipe */
            close(fds[0]);  /* close read end */
            signal(SIGPIPE, SIG_IGN);
            sleep(1);       /* let parent close read end */
            char buf[4] = "hi\n";
            int w = write(fds[1], buf, 3);
            /* With SIGPIPE ignored, write should return -1 */
            if (w < 0)
                _exit(0);   /* PASS: got error */
            else
                _exit(1);   /* FAIL: write succeeded on broken pipe */
        }

        /* Parent closes both ends */
        close(fds[0]);
        close(fds[1]);

        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "SIGPIPE-ignored: child exited normally",
             WIFEXITED(status));
        TEST(group, "SIGPIPE-ignored: write returned error (exit 0)",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    /* ── 3. Pipe EOF when all writers close ─────────────────────────────── */
    {
        int fds[2];
        int r = pipe(fds);
        TEST(group, "pipe-EOF: pipe created", r == 0);

        pid_t pid = fork();
        TEST(group, "pipe-EOF: fork succeeded", pid >= 0);

        if (pid == 0) {
            /* Child: reader */
            close(fds[1]);  /* close write end */
            char buf[16];
            int n = read(fds[0], buf, sizeof(buf));
            if (n == 0)
                _exit(0);   /* PASS: got EOF */
            else
                _exit(1);   /* FAIL */
        }

        /* Parent: close write end → child should get EOF */
        close(fds[1]);
        close(fds[0]);

        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "pipe-EOF: reader got EOF (exit 0)",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    ut_group_end(group);
}
