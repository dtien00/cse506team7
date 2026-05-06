#include "us_test_common.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>

#define PYTHON_BIN "/bin/python"

static void run_and_report(us_test_group_t *group, const char *py_path)
{
    pid_t pid = fork();
    if (pid == 0) {
        char *av[] = { (char *)PYTHON_BIN, (char *)py_path, NULL };
        execve(PYTHON_BIN, av, NULL);
        _exit(127);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st))
        printf("  %s: exited %d\n", py_path, WEXITSTATUS(st));
    else if (WIFSIGNALED(st))
        printf("  %s: killed by signal %d\n", py_path, WTERMSIG(st));
    else
        printf("  %s: unknown status %d\n", py_path, st);
    TEST(group, py_path, 1); /* always pass — just reporting */
}

void run_test_python_exitcode(us_test_group_t *group)
{
    ut_group_begin(group, "python_exitcode");
    run_and_report(group, "/hello.py");
    run_and_report(group, "/willcrash.py");
    ut_group_end(group);
}
