#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

static void check_exec_exit(us_test_group_t *group,
                            const char *name,
                            const char *path,
                            char *const argv[],
                            int expected_exit) {
    pid_t pid = fork();
    TEST(group, name, pid >= 0);
    if (pid == 0) {
        execve(path, argv, NULL);
        _exit(127);
    }
    if (pid > 0) {
        int status = 0;
        pid_t w = waitpid(pid, &status, 0);
        TEST(group, "exec child reaped", w == pid);
        TEST(group, "exec child exit status matches expected",
             WIFEXITED(status) && WEXITSTATUS(status) == expected_exit);
    }
}

void run_test_exec_bin(us_test_group_t *group) {
    ut_group_begin(group, "exec+bin");

    {
        char *argv[] = { "true", NULL };
        check_exec_exit(group, "exec /bin/true succeeds", "/bin/true", argv, 0);
    }

    {
        char *argv[] = { "false", NULL };
        check_exec_exit(group, "exec /bin/false returns failure exit code", "/bin/false", argv, 1);
    }

    {
        int pipefd[2];
        int rc = pipe(pipefd);
        TEST(group, "echo capture pipe created", rc == 0);
        if (rc == 0) {
            pid_t pid = fork();
            TEST(group, "exec /bin/echo fork succeeds", pid >= 0);
            if (pid == 0) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
                {
                    char *argv[] = { "echo", "hello", "world", NULL };
                    execve("/bin/echo", argv, NULL);
                }
                _exit(127);
            }
            if (pid > 0) {
                char buf[32];
                int status = 0;
                ssize_t n;
                ssize_t total = 0;

                close(pipefd[1]);
                while (total < (ssize_t)(sizeof(buf) - 1)) {
                    n = read(pipefd[0], buf + total, sizeof(buf) - 1 - (size_t)total);
                    if (n <= 0) {
                        break;
                    }
                    total += n;
                }
                buf[total > 0 ? total : 0] = '\0';
                close(pipefd[0]);

                TEST(group, "exec /bin/echo writes expected stdout",
                     total > 0 && strcmp(buf, "hello world\n") == 0);

                TEST(group, "exec /bin/echo child reaped", waitpid(pid, &status, 0) == pid);
                TEST(group, "exec /bin/echo exits successfully",
                     WIFEXITED(status) && WEXITSTATUS(status) == 0);
            }
        }
    }

    {
        pid_t pid = fork();
        TEST(group, "missing binary fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "missing", NULL };
            _exit(execve("/bin/definitely_missing", argv, NULL) < 0 ? 0 : 1);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "missing binary exec fails without replacing child",
                 waitpid(pid, &status, 0) == pid &&
                 WIFEXITED(status) &&
                 WEXITSTATUS(status) == 0);
        }
    }

    {
        pid_t pid = fork();
        TEST(group, "non-ELF target fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "hello.txt", NULL };
            _exit(execve("/hello.txt", argv, NULL) < 0 ? 0 : 1);
        }
        if (pid > 0) {
            int status = 0;
            TEST(group, "execing plain text file fails cleanly",
                 waitpid(pid, &status, 0) == pid &&
                 WIFEXITED(status) &&
                 WEXITSTATUS(status) == 0);
        }
    }

    ut_group_end(group);
}
