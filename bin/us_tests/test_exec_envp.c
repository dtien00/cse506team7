/*
 * test_exec_envp.c — execve envp + getenv/setenv/unsetenv tests
 *
 * Two groups:
 *   1. Direct in-process tests: getenv/setenv/unsetenv via the environ-backed
 *      stdlib.  Verifies the new implementation without needing fork/exec.
 *
 *   2. Exec-path tests: fork + execve with an envp, child re-execs this binary
 *      in a probe mode, calls getenv(), and exits 0 on success.  Verifies the
 *      full path: kernel stack → crt.S sets environ → getenv() finds the key.
 *
 * Probe modes (handled early in us_test.c main before any test infra runs):
 *   --envp-probe KEY=VALUE   exit 0 if getenv(KEY) == VALUE, else non-zero
 *   --envp-empty             exit 0 if getenv("SBX_SENTINEL") == NULL
 */

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "us_test_common.h"

/* ------------------------------------------------------------------ group 1 */

static void test_env_direct(us_test_group_t *g) {
    char *v;

    ut_group_begin(g, "getenv/setenv/unsetenv");

    /* getenv on a key that was never set must return NULL */
    v = getenv("_SBUNIX_LIBC_TEST_VAR_");
    TEST(g, "getenv unset returns NULL", v == NULL);

    /* setenv then getenv */
    setenv("_SBUNIX_LIBC_TEST_VAR_", "hello", 1);
    v = getenv("_SBUNIX_LIBC_TEST_VAR_");
    TEST(g, "getenv after setenv", v != NULL && strcmp(v, "hello") == 0);

    /* overwrite=0 must not change existing value */
    setenv("_SBUNIX_LIBC_TEST_VAR_", "world", 0);
    v = getenv("_SBUNIX_LIBC_TEST_VAR_");
    TEST(g, "setenv overwrite=0 keeps old value", v != NULL && strcmp(v, "hello") == 0);

    /* overwrite=1 changes value */
    setenv("_SBUNIX_LIBC_TEST_VAR_", "world", 1);
    v = getenv("_SBUNIX_LIBC_TEST_VAR_");
    TEST(g, "setenv overwrite=1 updates value", v != NULL && strcmp(v, "world") == 0);

    /* unsetenv removes the key */
    unsetenv("_SBUNIX_LIBC_TEST_VAR_");
    v = getenv("_SBUNIX_LIBC_TEST_VAR_");
    TEST(g, "getenv after unsetenv returns NULL", v == NULL);

    /* unsetenv on a non-existent key must not crash */
    unsetenv("_SBUNIX_LIBC_TEST_VAR_");
    TEST(g, "unsetenv non-existent no crash", 1);

    /* Multiple independent keys coexist */
    setenv("SBUNIX_A", "aaa", 1);
    setenv("SBUNIX_B", "bbb", 1);
    v = getenv("SBUNIX_A");
    TEST(g, "multiple keys: first key survives second setenv", v != NULL && strcmp(v, "aaa") == 0);
    v = getenv("SBUNIX_B");
    TEST(g, "multiple keys: second key present", v != NULL && strcmp(v, "bbb") == 0);
    unsetenv("SBUNIX_A");
    v = getenv("SBUNIX_A");
    TEST(g, "multiple keys: unset first, second still present",
         v == NULL && (v = getenv("SBUNIX_B")) != NULL && strcmp(v, "bbb") == 0);
    unsetenv("SBUNIX_B");

    ut_group_end(g);
}

/* ------------------------------------------------------------------ group 2 */

static void test_env_exec(us_test_group_t *g) {
    ut_group_begin(g, "execve envp");

    /* Test 1: envp key visible via getenv() in child after execve */
    {
        pid_t pid = fork();
        TEST(g, "envp/present: fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "us_tests", "--envp-probe", "SBX_ENV_TEST=hello", NULL };
            char *envp[] = { "SBX_ENV_TEST=hello", "PATH=/bin", NULL };
            execve("/bin/us_tests", argv, envp);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(g, "envp/present: waitpid returns child",  w == pid);
            TEST(g, "envp/present: child exited normally",  WIFEXITED(status));
            TEST(g, "envp/present: getenv found key in child",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* Test 2: empty envp — getenv() must return NULL in child */
    {
        pid_t pid = fork();
        TEST(g, "envp/empty: fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "us_tests", "--envp-empty", NULL };
            char *envp[] = { NULL };
            execve("/bin/us_tests", argv, envp);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(g, "envp/empty: waitpid returns child",  w == pid);
            TEST(g, "envp/empty: child exited normally",  WIFEXITED(status));
            TEST(g, "envp/empty: getenv returns NULL in child",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    /* Test 3: NULL envp — child sees empty environment */
    {
        pid_t pid = fork();
        TEST(g, "envp/null: fork succeeds", pid >= 0);
        if (pid == 0) {
            char *argv[] = { "us_tests", "--envp-empty", NULL };
            execve("/bin/us_tests", argv, NULL);
            _exit(127);
        }
        if (pid > 0) {
            int status = 0;
            pid_t w = waitpid(pid, &status, 0);
            TEST(g, "envp/null: waitpid returns child",  w == pid);
            TEST(g, "envp/null: child exited normally",  WIFEXITED(status));
            TEST(g, "envp/null: getenv returns NULL in child",
                 WIFEXITED(status) && WEXITSTATUS(status) == 0);
        }
    }

    ut_group_end(g);
}

/* ---------------------------------------------------------------- entry point */

void run_test_exec_envp(us_test_group_t groups[], int *idx) {
    test_env_direct(&groups[(*idx)++]);
    test_env_exec  (&groups[(*idx)++]);
}
