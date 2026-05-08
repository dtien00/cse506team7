#include "us_test_common.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#define PYTHON_BIN "/bin/python"
#define PYTHON_SMOKE_PATH "/test.py"

void run_test_python_smoke(us_test_group_t *group)
{
    ut_group_begin(group, "python_smoke");

    pid_t pid = fork();
    if (pid == 0) {
        char *av[] = { (char *)PYTHON_BIN, (char *)PYTHON_SMOKE_PATH, NULL };
        execve(PYTHON_BIN, av, NULL);
        _exit(127);
    }
    if (pid < 0) {
        TEST(group, "fork", 0);
        ut_group_end(group);
        return;
    }

    int st = 0;
    waitpid(pid, &st, 0);

    int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    TEST(group, "run /test.py", ok);
    if (!ok) {
        if (WIFEXITED(st))
            printf("  exit=%d\n", WEXITSTATUS(st));
        else if (WIFSIGNALED(st))
            printf("  signal=%d\n", WTERMSIG(st));
        else
            printf("  status=%d\n", st);
    }

    ut_group_end(group);
}

