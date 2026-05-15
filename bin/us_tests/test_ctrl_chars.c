/*
 * test_ctrl_chars.c — End-to-end userspace tests for terminal control chars.
 *
 * Exercises the kernel line-discipline path (kernel/tty/term.c::term_handle_input)
 * by injecting bytes into the console terminal via the test-only syscall
 * tty_inject(c). This lets us verify the same code paths a real keypress
 * would trigger, without driving the QEMU UART from the host side.
 *
 * Coverage:
 *   1. ^D (VEOF=0x04) on empty canonical line -> read() returns 0 (EOF).
 *   2. ^D after a partial line "abc"          -> read() returns 3 bytes "abc"
 *      (no trailing newline; line completed without it, per POSIX).
 *   3. ^Z (VSUSP=0x1A) -> SIGTSTP delivered to the foreground pgid; child
 *      observed via waitpid(WUNTRACED) as WIFSTOPPED + WSTOPSIG==SIGTSTP.
 *   4. ^C (VINTR=0x03) -> SIGINT delivered to the foreground pgid; child
 *      observed via waitpid as WIFSIGNALED + WTERMSIG==SIGINT.
 */

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <sys/wait.h>
#include <sched.h>
#include "us_test_common.h"

extern int tty_inject(int c);

/* Spin a few yields so a freshly forked child reaches its blocking syscall
 * before the parent injects. The pipe sentinel guarantees ordering up to
 * the moment the child returns from write(); the yields cover the gap
 * between that and entering read()/sleep(). */
static void wait_for_child_to_block(void) {
    for (int i = 0; i < 50; i++) sched_yield();
}

void run_test_ctrl_chars(us_test_group_t *group) {
    ut_group_begin(group, "ctrl_chars");

    /* Stdin must be a tty for any of this to make sense. */
    struct termios saved_tio;
    int gt = tcgetattr(STDIN_FILENO, &saved_tio);
    if (gt != 0) {
        SKIP(group, "ctrl_d_eof: read() returned 0 (EOF)", "stdin is not a tty");
        SKIP(group, "ctrl_d_partial: read() returned 3 bytes 'abc'", "stdin is not a tty");
        SKIP(group, "ctrl_z_sigtstp: WSTOPSIG == SIGTSTP", "stdin is not a tty");
        SKIP(group, "ctrl_c_sigint: WTERMSIG == SIGINT", "stdin is not a tty");
        ut_group_end(group);
        return;
    }

    /* Force canonical mode + ISIG, with ECHO off so the test output
     * isn't polluted by "^C\n"/"^Z\n" echoes. */
    struct termios cooked = saved_tio;
    cooked.c_lflag |= (ICANON | ISIG);
    cooked.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &cooked);

    pid_t saved_fg = tcgetpgrp(STDIN_FILENO);

    /* ── 1. ^D on empty line ─────────────────────────────────────────────── */
    {
        int rdy[2];
        TEST(group, "ctrl_d_eof: pipe()", pipe(rdy) == 0);

        pid_t pid = fork();
        TEST(group, "ctrl_d_eof: fork()", pid >= 0);

        if (pid == 0) {
            close(rdy[0]);
            char r = 'R';
            write(rdy[1], &r, 1);
            close(rdy[1]);

            char buf[16];
            int n = read(STDIN_FILENO, buf, sizeof buf);
            /* 100 = EOF (n==0), 101 = unexpected data, 102 = read error */
            if (n == 0)      _exit(100);
            else if (n > 0)  _exit(101);
            else             _exit(102);
        }
        if (pid > 0) {
            close(rdy[1]);
            char b;
            read(rdy[0], &b, 1);
            close(rdy[0]);
            wait_for_child_to_block();

            tty_inject(0x04);   /* ^D */

            int status;
            pid_t r = waitpid(pid, &status, 0);
            TEST(group, "ctrl_d_eof: waitpid reaped child", r == pid);
            TEST(group, "ctrl_d_eof: read() returned 0 (EOF)",
                 WIFEXITED(status) && WEXITSTATUS(status) == 100);

            if (!(WIFEXITED(status) && WEXITSTATUS(status) == 100)) {
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
            }
        }
    }

    /* ── 2. ^D after partial line "abc" ──────────────────────────────────── */
    {
        int rdy[2];
        TEST(group, "ctrl_d_partial: pipe()", pipe(rdy) == 0);

        pid_t pid = fork();
        TEST(group, "ctrl_d_partial: fork()", pid >= 0);

        if (pid == 0) {
            close(rdy[0]);
            char r = 'R';
            write(rdy[1], &r, 1);
            close(rdy[1]);

            char buf[16];
            int n = read(STDIN_FILENO, buf, sizeof buf);
            if (n == 3 && buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c')
                _exit(50);
            _exit(99);
        }
        if (pid > 0) {
            close(rdy[1]);
            char b;
            read(rdy[0], &b, 1);
            close(rdy[0]);
            wait_for_child_to_block();

            tty_inject('a');
            tty_inject('b');
            tty_inject('c');
            tty_inject(0x04);   /* ^D — completes line without newline */

            int status;
            pid_t r = waitpid(pid, &status, 0);
            TEST(group, "ctrl_d_partial: waitpid reaped child", r == pid);
            TEST(group, "ctrl_d_partial: read() returned 3 bytes 'abc'",
                 WIFEXITED(status) && WEXITSTATUS(status) == 50);

            if (!(WIFEXITED(status) && WEXITSTATUS(status) == 50)) {
                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);
            }
        }
    }

    /* ── 3. ^Z → SIGTSTP delivered to foreground pgid ────────────────────── */
    {
        int rdy[2];
        TEST(group, "ctrl_z_sigtstp: pipe()", pipe(rdy) == 0);

        pid_t pid = fork();
        TEST(group, "ctrl_z_sigtstp: fork()", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);                     /* own pgrp */
            tcsetpgrp(STDIN_FILENO, getpid()); /* claim foreground */
            close(rdy[0]);
            char r = 'R';
            write(rdy[1], &r, 1);
            close(rdy[1]);
            for (;;) sleep(60);                /* wait to be stopped */
            _exit(0);
        }
        if (pid > 0) {
            setpgid(pid, pid);                 /* race-defensive */
            close(rdy[1]);
            char b;
            read(rdy[0], &b, 1);
            close(rdy[0]);
            wait_for_child_to_block();

            tty_inject(0x1A);   /* ^Z */

            int status;
            pid_t r = waitpid(pid, &status, WUNTRACED);
            TEST(group, "ctrl_z_sigtstp: waitpid(WUNTRACED) returned child",
                 r == pid);
            TEST(group, "ctrl_z_sigtstp: child is WIFSTOPPED",
                 WIFSTOPPED(status));
            TEST(group, "ctrl_z_sigtstp: WSTOPSIG == SIGTSTP",
                 WIFSTOPPED(status) && WSTOPSIG(status) == SIGTSTP);

            /* Restore terminal foreground BEFORE killing the child so the
             * test process keeps owning the tty. */
            if (saved_fg > 0) tcsetpgrp(STDIN_FILENO, saved_fg);
            kill(-pid, SIGCONT);
            kill(-pid, SIGKILL);
            waitpid(pid, NULL, 0);
        }
    }

    /* ── 4. ^C → SIGINT delivered to foreground pgid ─────────────────────── */
    {
        int rdy[2];
        TEST(group, "ctrl_c_sigint: pipe()", pipe(rdy) == 0);

        pid_t pid = fork();
        TEST(group, "ctrl_c_sigint: fork()", pid >= 0);

        if (pid == 0) {
            setpgid(0, 0);
            tcsetpgrp(STDIN_FILENO, getpid());
            close(rdy[0]);
            char r = 'R';
            write(rdy[1], &r, 1);
            close(rdy[1]);
            for (;;) sleep(60);
            _exit(0);
        }
        if (pid > 0) {
            setpgid(pid, pid);
            close(rdy[1]);
            char b;
            read(rdy[0], &b, 1);
            close(rdy[0]);
            wait_for_child_to_block();

            tty_inject(0x03);   /* ^C */

            int status;
            pid_t r = waitpid(pid, &status, 0);
            TEST(group, "ctrl_c_sigint: waitpid reaped child", r == pid);
            TEST(group, "ctrl_c_sigint: WIFSIGNALED", WIFSIGNALED(status));
            TEST(group, "ctrl_c_sigint: WTERMSIG == SIGINT",
                 WIFSIGNALED(status) && WTERMSIG(status) == SIGINT);

            if (saved_fg > 0) tcsetpgrp(STDIN_FILENO, saved_fg);
        }
    }

    /* Restore terminal state. */
    if (saved_fg > 0) tcsetpgrp(STDIN_FILENO, saved_fg);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);

    ut_group_end(group);
}
