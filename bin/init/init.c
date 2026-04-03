#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

int main(void) {
    /* fds 0/1/2 already open — fd_init set them up in the kernel */

    /* Run the test suite before starting the shell. */
    {
        pid_t ts_pid = fork();
        if (ts_pid == 0) {
            char *argv[] = { "testsuite", NULL };
            execve("/bin/testsuite", argv, NULL);
            write(STDERR_FILENO, "init: exec /bin/testsuite failed\n", 33);
            exit(1);
        }
        if (ts_pid > 0) {
            int status;
            wait(&status);
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