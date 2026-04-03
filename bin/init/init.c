#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    /* fds 0/1/2 already open — fd_init set them up in the kernel */

    /*
     * User-space test suite — runs before the shell so results are always
     * visible at boot.  Each binary reports its own [PASS]/[FAIL] lines;
     * init collects the exit codes and prints a final summary.
     *
     * Add new test binaries to this table.  Each entry is:
     *   { display_name, path }
     */
    {
        static const struct { const char *name; const char *path; } tests[] = {
            { "libc / stdlib / malloc",  "/bin/libc_test" },
            { "uptime / sleep / kill",   "/bin/sigtest"   },
        };
        static const int N_TESTS = (int)(sizeof(tests) / sizeof(tests[0]));

#define SUITE_OPEN "\n========================================\n" \
                   "       SBUnix User-Space Test Suite\n" \
                   "========================================\n\n"
        write(STDOUT_FILENO, SUITE_OPEN, sizeof(SUITE_OPEN) - 1);
#undef SUITE_OPEN

        int suite_failed = 0;

        for (int i = 0; i < N_TESTS; i++) {
            /* Print which suite is starting */
            write(STDOUT_FILENO, "--- Running: ", 13);
            write(STDOUT_FILENO, tests[i].name, strlen(tests[i].name));
            write(STDOUT_FILENO, " ---\n", 5);

            pid_t test_pid = fork();
            if (test_pid == 0) {
                char *argv[] = { (char *)tests[i].name, NULL };
                execve(tests[i].path, argv, NULL);
                /* execve failed — report and exit non-zero */
                write(STDERR_FILENO, "init: exec failed: ", 19);
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

#define SUITE_CLOSE "\n========================================\n" \
                    "       Test Suite Complete\n" \
                    "========================================\n\n"
        write(STDOUT_FILENO, SUITE_CLOSE, sizeof(SUITE_CLOSE) - 1);
#undef SUITE_CLOSE

        if (suite_failed) {
            write(STDOUT_FILENO, "WARN: some test suites FAILED — check output above\n\n", 52);
        }
        /* Always continue to the shell regardless of test outcome */
    }

    while (1) {
        pid_t sh_pid = fork();

        if (sh_pid < 0) {
            write(STDERR_FILENO, "init: fork failed\n", 18);
            sleep(1);
            continue;
        }

        if (sh_pid == 0) {
            /* child: exec the shell */
            char *argv[] = { "sh", NULL };
            execve("/bin/sh", argv, NULL);
            write(STDERR_FILENO, "init: exec /bin/sh failed\n", 26);
            exit(1);
        }

        /*
         * Parent: reap ALL children, not just sh.
         * This handles orphans reparented to init by proc_exit().
         * When sh itself exits, break and restart it.
         */
        while (1) {
            int status;
            pid_t exited = wait(&status);

            if (exited < 0) {
                /* no more children — shouldn't happen but be safe */
                break;
            }

            if (exited == sh_pid) {
                /* shell exited — log and restart */
                write(STDOUT_FILENO, "init: sh exited, restarting\n", 28);
                break;
            }
            /* else: orphan reaped silently, keep waiting */
        }
    }

    /* unreachable */
    return 0;
}