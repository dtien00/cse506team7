#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_process(us_test_group_t *group) {
    pid_t pid;
    pid_t w;
    int status = 0;

    ut_group_begin(group, "process.h");

    TEST(group, "getpid returns positive", getpid() > 0);

    pid = fork();
    TEST(group, "fork returns non-negative", pid >= 0);

    if (pid == 0) {
        _exit(7);
    }

    if (pid > 0) {
        w = waitpid(pid, &status, 0);
        TEST(group, "waitpid returns child pid", w == pid);
        TEST(group, "child exits normally", WIFEXITED(status));
        TEST(group, "child exit code propagated", WEXITSTATUS(status) == 7);
    }

    // --- waitpid(-1, ...) behaves like wait() ---
    // Fork a child that exits with code 3; waitpid(-1) must reap it.
    pid = fork();
    if (pid == 0) {
        _exit(3);
    }
    if (pid > 0) {
        status = 0;
        w = waitpid(-1, &status, 0);
        TEST(group, "waitpid(-1) returns child pid", w == pid);
        TEST(group, "waitpid(-1) child exited normally", WIFEXITED(status));
        TEST(group, "waitpid(-1) exit code correct", WEXITSTATUS(status) == 3);
    }

    // --- WNOHANG on a live child returns 0 immediately ---
    // The child loops with sleep() so it makes syscalls — kill's proc_unblock
    // wakes it from sleep, proc_sleep() then checks pending_signals and exits.
    // A pure spin loop would never make a syscall and SIGKILL would never fire.
    pid = fork();
    if (pid == 0) {
        while (1) sleep(1);
        _exit(0);
    }
    if (pid > 0) {
        w = waitpid(pid, NULL, WNOHANG);
        TEST(group, "WNOHANG on live child returns 0", w == 0);
        // Send SIGKILL; child is in sleep() so it will be woken and check signals.
        kill(pid, 9);
        waitpid(pid, NULL, 0);
    }

    // --- waitpid for non-child PID returns -1 ---
    w = waitpid(9999, NULL, 0);
    TEST(group, "waitpid non-child PID returns -1", w == -1);

    // --- fork: child resumes at fork call point, not at program entry ---
    // The child writes a sentinel byte through a pipe immediately after fork.
    // If fork were broken and the child restarted from main(), it would call
    // pipe()/fork() again and never write to this pipe — the parent's read()
    // would block indefinitely (exactly the hang the feedback described).
    {
        int pipefd[2];
        int rc = pipe(pipefd);
        TEST(group, "fork-resume: pipe created", rc == 0);
        if (rc == 0) {
            pid = fork();
            TEST(group, "fork-resume: fork succeeded", pid >= 0);
            if (pid == 0) {
                close(pipefd[0]);
                char sentinel = 0x42;
                write(pipefd[1], &sentinel, 1);
                close(pipefd[1]);
                _exit(0);
            }
            if (pid > 0) {
                close(pipefd[1]);
                char buf = 0;
                ssize_t n = read(pipefd[0], &buf, 1);
                close(pipefd[0]);
                TEST(group, "fork-resume: child wrote sentinel (resumed at fork, not at main)",
                     n == 1 && buf == 0x42);
                status = 0;
                waitpid(pid, &status, 0);
                TEST(group, "fork-resume: child exited cleanly after fork",
                     WIFEXITED(status) && WEXITSTATUS(status) == 0);
            }
        }
    }

    ut_group_end(group);
}
