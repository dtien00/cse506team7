#include "us_test_common.h"
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

/*
 * Tests for non-interactive shell invocation via "sh -c <cmd>".
 *
 * POSIX requires that when a shell is invoked with -c the shell executes the
 * command string given as the next argument and exits with its status, rather
 * than entering an interactive read-loop.  Without this, any program that
 * invokes /bin/sh -c <cmd> (system(3), popen(3), make, scripts, ...) will
 * hang waiting for interactive input.
 *
 * These tests are deliberately lightweight (one fork per check) so they can
 * run in the default RUN_ALL_TESTS path without straining physical memory.
 */

/* Fork a child, exec /bin/sh -c cmd_str, return raw waitpid status or -1. */
static int sh_c(const char *cmd_str) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        char *argv[] = { "sh", "-c", (char *)cmd_str, NULL };
        execve("/bin/sh", argv, NULL);
        _exit(127);
    }
    int status = -1;
    waitpid(pid, &status, 0);
    return status;
}

/* ── 1. Basic non-interactive execution ──────────────────────────────────── */

static void test_sh_c_exits(us_test_group_t *tg) {
    int s = sh_c("exit 0");
    TEST(tg, "sh -c 'exit 0': exits (does not hang in interactive loop)",
         WIFEXITED(s));
    TEST(tg, "sh -c 'exit 0': exit code 0",
         WIFEXITED(s) && WEXITSTATUS(s) == 0);

    s = sh_c("exit 42");
    TEST(tg, "sh -c 'exit 42': exits normally", WIFEXITED(s));
    TEST(tg, "sh -c 'exit 42': exit code propagated", WEXITSTATUS(s) == 42);
}

/* ── 2. Command string is actually executed ───────────────────────────────── */

static void test_sh_c_runs_command(us_test_group_t *tg) {
    /* Write a sentinel to a temp file via sh -c, then verify it was created. */
    unlink("/mnt/sh_c_probe.txt");

    int s = sh_c("echo hi > /mnt/sh_c_probe.txt");
    TEST(tg, "sh -c redirect: exits normally", WIFEXITED(s));

    int fd = open("/mnt/sh_c_probe.txt", O_RDONLY);
    TEST(tg, "sh -c redirect: output file was created", fd >= 0);
    if (fd >= 0) {
        char buf[8] = {0};
        int n = (int)read(fd, buf, sizeof(buf) - 1);
        close(fd);
        TEST(tg, "sh -c redirect: file contains shell output",
             n > 0 && buf[0] == 'h' && buf[1] == 'i');
    }
    unlink("/mnt/sh_c_probe.txt");
}

/* ── 3. Exit status of the executed command is forwarded ─────────────────── */

static void test_sh_c_status(us_test_group_t *tg) {
    int s;

    s = sh_c("true");
    TEST(tg, "sh -c 'true': exits 0", WIFEXITED(s) && WEXITSTATUS(s) == 0);

    s = sh_c("false");
    TEST(tg, "sh -c 'false': exits non-zero", WIFEXITED(s) && WEXITSTATUS(s) != 0);

    /* /nonexistent should produce exit 127 (command not found) */
    s = sh_c("/nonexistent_xyzzy_cmd");
    TEST(tg, "sh -c missing-cmd: exits 127", WIFEXITED(s) && WEXITSTATUS(s) == 127);
}

/* ── 4. Only the given command string runs; no extra lines consumed ───────── */

static void test_sh_c_single_command(us_test_group_t *tg) {
    /* "exit 3; exit 9" — only the first should run; shell must exit 3. */
    int s = sh_c("exit 3; exit 9");
    TEST(tg, "sh -c 'exit 3; exit 9': exits normally", WIFEXITED(s));
    TEST(tg, "sh -c 'exit 3; exit 9': code == 3 (stops after first exit)",
         WEXITSTATUS(s) == 3);
}

/* ── public entry point ──────────────────────────────────────────────────── */

void run_test_sh_dash_c(us_test_group_t *group) {
    ut_group_begin(group, "sh_dash_c");
    test_sh_c_exits(group);
    test_sh_c_runs_command(group);
    test_sh_c_status(group);
    test_sh_c_single_command(group);
    ut_group_end(group);
}
