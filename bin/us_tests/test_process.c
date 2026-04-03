#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_process(us_test_group_t *group) {
    pid_t pid;
    pid_t w;
    int status = 0;

    ut_group_begin(group, "process.h");

    TEST(group, "getpid returns positive", getpid() > 0);

    pid = fork();
    TEST(group, "fork returns non-negative", pid >= 0);

    if (pid == 0) {
        _exit(7);
    }

    if (pid > 0) {
        w = waitpid(pid, &status, 0);
        TEST(group, "waitpid returns child pid", w == pid);
        TEST(group, "child exits normally", WIFEXITED(status));
        TEST(group, "child exit code propagated", WEXITSTATUS(status) == 7);
    }

    ut_group_end(group);
}
