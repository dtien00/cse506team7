#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void write_fd(int fd, const char *msg) {
    write(fd, msg, strlen(msg));
}

static pid_t spawn_exec(const char *path,
                        char *const argv[],
                        const char *fork_err,
                        const char *exec_err) {
    pid_t pid = fork();

    if (pid < 0) {
        write_fd(STDERR_FILENO, fork_err);
        return -1;
    }

    if (pid == 0) {
        execve(path, argv, NULL);
        write_fd(STDERR_FILENO, exec_err);
        _exit(1);
    }

    return pid;
}

static pid_t wait_for_pid(pid_t target, int *status_out) {
    while (1) {
        int status = 0;
        pid_t exited = wait(&status);

        if (exited < 0) {
            return -1;
        }

        if (exited == target) {
            if (status_out)
                *status_out = status;
            return exited;
        }
        // Other children (e.g. orphans) are reaped and ignored.
    }
}

static void run_userspace_tests(void) {
    char *argv[] = { "us_tests", NULL };
    int status = 0;

    pid_t test_pid = spawn_exec("/bin/us_tests",
                                argv,
                                "init: fork for /bin/us_tests failed\n",
                                "init: exec /bin/us_tests failed\n");
    if (test_pid < 0)
        return;

    if (wait_for_pid(test_pid, &status) < 0) {
        write_fd(STDERR_FILENO, "init: wait for /bin/us_tests failed\n");
        return;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        write_fd(STDOUT_FILENO, "init: userspace tests reported failures\n");
    } else {
        write_fd(STDOUT_FILENO, "init: userspace tests passed\n");
    }
}

int main(void) {
    /* fds 0/1/2 already open — fd_init set them up in the kernel */
    run_userspace_tests();

    while (1) {
        char *argv[] = { "sh", NULL };
        pid_t sh_pid = spawn_exec("/bin/sh",
                                  argv,
                                  "init: fork failed\n",
                                  "init: exec /bin/sh failed\n");

        if (sh_pid < 0) {
            sleep(1);
            continue;
        }

        if (wait_for_pid(sh_pid, NULL) < 0) {
            break;
        }

        write_fd(STDOUT_FILENO, "init: sh exited, restarting\n");
    }

    return 0;
}