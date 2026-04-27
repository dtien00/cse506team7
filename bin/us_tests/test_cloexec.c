/*
 * test_cloexec.c — Tests for CLOSE_ON_EXEC (O_CLOEXEC / FD_CLOEXEC).
 *
 * Uses cloexec_helper: a small binary that tries to read from an fd passed as
 * argv[1] and exits 0 if the fd was closed (read < 0) or 1 if it is still open.
 *
 * Test matrix:
 *   enforced  — open with O_CLOEXEC; fd must be closed after exec (helper exits 0)
 *   not_set   — open without O_CLOEXEC; fd must survive exec    (helper exits 1)
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>
#include "us_test_common.h"

#define HELPER   "/bin/cloexec_helper"
#define TESTFILE "/hello.txt"

/* Exec the helper with the fd number and return its exit status, or -1. */
static int run_helper(int fd) {
    char fd_str[16];
    snprintf(fd_str, sizeof(fd_str), "%d", fd);

    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        char *argv[] = { HELPER, fd_str, (char *)0 };
        execve(HELPER, argv, (char *const *)0);
        _exit(2);   /* execve failed */
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return -1;

    return (WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}

static void test_cloexec_enforced(us_test_group_t *group) {
    int fd = open(TESTFILE, O_RDONLY | O_CLOEXEC);
    TEST(group, "cloexec-enforced: open with O_CLOEXEC succeeds", fd >= 0);
    if (fd < 0) return;

    int result = run_helper(fd);
    close(fd);

    TEST(group, "cloexec-enforced: helper exits 0 (fd closed after exec)",
         result == 0);
}

static void test_cloexec_not_set(us_test_group_t *group) {
    int fd = open(TESTFILE, O_RDONLY);
    TEST(group, "cloexec-not-set: open without O_CLOEXEC succeeds", fd >= 0);
    if (fd < 0) return;

    int result = run_helper(fd);
    close(fd);

    TEST(group, "cloexec-not-set: helper exits 1 (fd survives exec without O_CLOEXEC)",
         result == 1);
}

void run_test_cloexec(us_test_group_t *group) {
    ut_group_begin(group, "close-on-exec");
    test_cloexec_enforced(group);
    test_cloexec_not_set(group);
    ut_group_end(group);
}
