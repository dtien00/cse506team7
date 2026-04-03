#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>

/*
 * SBUnix user-space test suite orchestrator.
 *
 * Each entry below is { display_name, path_to_binary }.
 * To add a new test:
 *   1. Create bin/<testname>/<testname>.c
 *   2. Add an entry to the tests[] table below — nothing else needs to change.
 *
 * Exit code: number of failed suites (0 = all passed).
 */
static const struct { const char *name; const char *path; } tests[] = {
    { "libc / stdlib / malloc",  "/bin/libc_test" },
    { "uptime / sleep / kill",   "/bin/sigtest"   },
};
static const int N_TESTS = (int)(sizeof(tests) / sizeof(tests[0]));

#define SUITE_OPEN  "\n========================================\n" \
                    "       SBUnix User-Space Test Suite\n" \
                    "========================================\n\n"

#define SUITE_CLOSE "\n========================================\n" \
                    "       Test Suite Complete\n" \
                    "========================================\n\n"

int main(void) {
    write(STDOUT_FILENO, SUITE_OPEN, sizeof(SUITE_OPEN) - 1);

    int suite_failed = 0;

    for (int i = 0; i < N_TESTS; i++) {
        write(STDOUT_FILENO, "--- Running: ", 13);
        write(STDOUT_FILENO, tests[i].name, strlen(tests[i].name));
        write(STDOUT_FILENO, " ---\n", 5);

        pid_t test_pid = fork();
        if (test_pid == 0) {
            char *argv[] = { (char *)tests[i].name, NULL };
            execve(tests[i].path, argv, NULL);
            write(STDERR_FILENO, "testsuite: exec failed: ", 24);
            write(STDERR_FILENO, tests[i].path, strlen(tests[i].path));
            write(STDERR_FILENO, "\n", 1);
            exit(1);
        }

        if (test_pid > 0) {
            int status = 0;
            wait(&status);
            if (status != 0) {
                write(STDOUT_FILENO, "[SUITE FAIL] ", 13);
                suite_failed++;
            } else {
                write(STDOUT_FILENO, "[SUITE PASS] ", 13);
            }
            write(STDOUT_FILENO, tests[i].name, strlen(tests[i].name));
            write(STDOUT_FILENO, "\n", 1);
        }
    }

    write(STDOUT_FILENO, SUITE_CLOSE, sizeof(SUITE_CLOSE) - 1);

    if (suite_failed) {
        write(STDOUT_FILENO, "WARN: some test suites FAILED — check output above\n\n", 52);
    }

    return suite_failed;
}
