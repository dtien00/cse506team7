/*
 * test_tcgetsid.c — Tests for tcgetsid() (libc/ioctl.c).
 *
 * tcgetsid(fd) returns the session ID of the foreground process group leader
 * of the terminal.  Our libc validates fd via tcgetattr (ENOTTY on non-tty)
 * and returns getpid() for valid tty fds — correct because every shell process
 * is its own session leader after setsid(), and the kernel lacks a getsid()
 * syscall for a more precise answer.
 * No fork() needed: all checks are pure libc comparisons.
 */

#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include "us_test_common.h"

void run_test_tcgetsid(us_test_group_t *group) {
    ut_group_begin(group, "tcgetsid");

    pid_t my_pid = getpid();

    /* Basic: call succeeds and returns current pid on a tty fd */
    pid_t sid_stdin = tcgetsid(STDIN_FILENO);
    TEST(group, "tcgetsid(stdin): returns current pid", sid_stdin == my_pid);

    /* stdout and stderr are the same terminal */
    pid_t sid_stdout = tcgetsid(STDOUT_FILENO);
    TEST(group, "tcgetsid(stdout): returns current pid", sid_stdout == my_pid);

    pid_t sid_stderr = tcgetsid(STDERR_FILENO);
    TEST(group, "tcgetsid(stderr): returns current pid", sid_stderr == my_pid);

    /* Consistency: all three standard fds return the same session id */
    TEST(group, "tcgetsid: consistent across std fds",
         sid_stdin == sid_stdout && sid_stdout == sid_stderr);

    /* ash job-control gate: tcgetsid(tty_fd) != getpid() must be false */
    TEST(group, "tcgetsid(stdin) == getpid(): job-control gate passes",
         tcgetsid(STDIN_FILENO) == getpid());

    /* Non-tty fd (regular file): must return -1 and set errno=ENOTTY (POSIX) */
    {
        int rfd = open("/hello.txt", 0 /* O_RDONLY */);
        if (rfd < 0) {
            SKIP(group, "tcgetsid(regular file): returns -1 (ENOTTY)", "open(/hello.txt) failed");
        } else {
            errno = 0;
            pid_t sid_file = tcgetsid(rfd);
            TEST(group, "tcgetsid(regular file): returns -1 (ENOTTY)",
                 sid_file == (pid_t)-1 && errno == ENOTTY);
            close(rfd);
        }
    }

    ut_group_end(group);
}