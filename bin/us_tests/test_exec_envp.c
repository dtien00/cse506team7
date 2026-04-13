#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_exec_envp(us_test_group_t *group) {
    ut_group_begin(group, "execve envp");

    {
        pid_t pid = fork();
        TEST(group, "envp/present: fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "us_tests", "--envp-probe", "SBX_ENV_TEST=hello", NULL };
            char *envp[] = { "SBX_ENV_TEST=hello", "PATH=/bin", NULL };
            execve("/bin/us_tests", argv, envp);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "envp/present: waitpid returns child", w == pid);
            TEST(group, "envp/present: child exited normally", WIFEXITED(status));
            TEST(group, "envp/present: probe exits 0", WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    {
        pid_t pid = fork();
        TEST(group, "envp/empty: fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "us_tests", "--envp-empty", NULL };
            char *envp[] = { NULL };
            execve("/bin/us_tests", argv, envp);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(group, "envp/empty: waitpid returns child", w == pid);
            TEST(group, "envp/empty: child exited normally", WIFEXITED(status));
            TEST(group, "envp/empty: probe exits 0", WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    ut_group_end(group);
}

