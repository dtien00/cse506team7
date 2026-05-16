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

/* ── 5. Append redirection (>>) ──────────────────────────────────────────── */

static void test_sh_c_append(us_test_group_t *tg) {
    const char *path = "/mnt/sh_append_test.txt";
    unlink(path);

    /* First command: write "line1\n" with truncating redirect. */
    int s = sh_c("echo line1 > /mnt/sh_append_test.txt");
    TEST(tg, "sh >> setup: truncate redirect exits 0",
         WIFEXITED(s) && WEXITSTATUS(s) == 0);

    /* Second command: append "line2\n" with append redirect. */
    s = sh_c("echo line2 >> /mnt/sh_append_test.txt");
    TEST(tg, "sh >> append: exits 0",
         WIFEXITED(s) && WEXITSTATUS(s) == 0);

    /* Read back the file and verify both lines are present in order. */
    int fd = open(path, O_RDONLY);
    TEST(tg, "sh >> result: file exists", fd >= 0);
    if (fd >= 0) {
        char buf[64];
        int n = (int)read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) buf[n] = '\0';
        TEST(tg, "sh >> result: contains first line",
             n > 0 && strstr(buf, "line1") != NULL);
        TEST(tg, "sh >> result: contains second line",
             n > 0 && strstr(buf, "line2") != NULL);
        /* "line1" must appear before "line2" — append didn't overwrite. */
        TEST(tg, "sh >> result: first line precedes second",
             n > 0 && strstr(buf, "line1") < strstr(buf, "line2"));
    }
    unlink(path);

    /* A second >> on a non-existent file must create it (O_CREAT). */
    unlink(path);
    s = sh_c("echo only >> /mnt/sh_append_test.txt");
    TEST(tg, "sh >> on new file: exits 0",
         WIFEXITED(s) && WEXITSTATUS(s) == 0);
    fd = open(path, O_RDONLY);
    TEST(tg, "sh >> on new file: file created", fd >= 0);
    if (fd >= 0) {
        char buf[32];
        int n = (int)read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) buf[n] = '\0';
        TEST(tg, "sh >> on new file: contains written text",
             n > 0 && strstr(buf, "only") != NULL);
    }
    unlink(path);

    /* Verify >> is a single operator: "echo x >>file" must NOT misparse
       the second '>' as a separate redirect that rejects 'file' as a path. */
    s = sh_c("echo token >> /mnt/sh_append_test.txt");
    TEST(tg, "sh >>: operator tokenised as single token (no syntax error)",
         WIFEXITED(s) && WEXITSTATUS(s) == 0);
    unlink(path);
}

/* ── public entry point ──────────────────────────────────────────────────── */

void run_test_sh_dash_c(us_test_group_t *group) {
    ut_group_begin(group, "sh_dash_c");
    test_sh_c_exits(group);
    test_sh_c_runs_command(group);
    test_sh_c_status(group);
    test_sh_c_single_command(group);
    test_sh_c_append(group);
    ut_group_end(group);
}
