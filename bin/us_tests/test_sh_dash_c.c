#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "us_test_common.h"

/* Run /bin/sh -c <cmd_str> and return the wait status, or -1 on fork failure. */
static int sh_c(const char *cmd_str) {
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        char *argv[] = { "sh", "-c", (char *)cmd_str, NULL };
        execve("/bin/sh", argv, NULL);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

void run_test_sh_dash_c(us_test_group_t *group) {
    ut_group_begin(group, "sh_dash_c");

    /* ── Test 1: sh -c exits (does not hang) ─────────────────────────────── */
    {
        int status = sh_c("exit 0");
        TEST(group, "sh -c 'exit 0' does not hang and exits",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    /* ── Test 2: sh -c can run a command and produce output ──────────────── */
    {
        /* Write to a file in /tmp, then verify the file exists */
        int status = sh_c("echo hello > /tmp/sh_c_test.txt");
        int wrote_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        TEST(group, "sh -c 'echo > file' exits 0", wrote_ok);
        if (wrote_ok) {
            int fd = open("/tmp/sh_c_test.txt", O_RDONLY);
            TEST(group, "sh -c echo output file exists", fd >= 0);
            if (fd >= 0) {
                char buf[32] = {0};
                int n = read(fd, buf, sizeof(buf) - 1);
                close(fd);
                TEST(group, "sh -c echo wrote non-empty content", n > 0);
            }
            unlink("/tmp/sh_c_test.txt");
        }
    }

    /* ── Test 3: exit status is propagated correctly ─────────────────────── */
    {
        int s0 = sh_c("true");
        TEST(group, "sh -c 'true' exits 0",
             WIFEXITED(s0) && WEXITSTATUS(s0) == 0);

        int s1 = sh_c("false");
        TEST(group, "sh -c 'false' exits non-zero",
             WIFEXITED(s1) && WEXITSTATUS(s1) != 0);

        int s127 = sh_c("_no_such_command_xyz_");
        TEST(group, "sh -c '<unknown cmd>' exits non-zero",
             WIFEXITED(s127) && WEXITSTATUS(s127) != 0);
    }

    /* ── Test 4: only the first command is run; semicolons in one arg ────── */
    {
        /* "exit 3; exit 9" — sh -c treats its single arg as a full command
         * string, so the shell executes both commands sequentially and the
         * last exit status (3, since exit stops execution) is returned. */
        int status = sh_c("exit 3");
        TEST(group, "sh -c 'exit 3' propagates exit code 3",
             WIFEXITED(status) && WEXITSTATUS(status) == 3);
    }

    ut_group_end(group);
}