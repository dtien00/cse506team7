#include "us_test_common.h"
#include <unistd.h>
#include <sys/wait.h>

// Verifies that a child's file descriptors are released when it exits,
// not when the parent reaps it. The pipe write end must be closed at
// child exit so the parent's read sees EOF. If it hangs, proc_destroy
// is not closing FDs before the process becomes a zombie.
static void test_zombie_pipe_eof(us_test_group_t *tg) {
    int fds[2];
    if (pipe(fds) != 0) {
        SKIP(tg, "zombie fd: pipe creation failed", "pipe() returned error");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        SKIP(tg, "zombie fd: fork failed", "fork() returned error");
        return;
    }

    if (pid == 0) {
        // Child: close read end, write a byte, then exit without calling waitpid.
        // The write end stays open until exit — if fd_destroy runs at exit,
        // the parent will see EOF immediately. If it only runs at reap, the
        // parent's read will block forever (the write end is still "open").
        close(fds[0]);
        char c = 'Z';
        write(fds[1], &c, 1);
        // Deliberately do NOT close fds[1] — let exit handle it.
        _exit(0);
    }

    // Parent: close our copy of the write end. Now the only remaining write
    // end is the child's. When the child exits and its FDs are released,
    // we should read the byte and then get EOF (read returns 0).
    close(fds[1]);

    char buf[4] = {0};
    int n = read(fds[0], buf, 1);
    TEST(tg, "zombie fd: parent reads child's written byte", n == 1 && buf[0] == 'Z');

    // This read must return 0 (EOF) before we call waitpid.
    // If the kernel only closes child FDs at reap time, this will block.
    int eof = read(fds[0], buf, 1);
    TEST(tg, "zombie fd: parent sees EOF before waitpid (FDs released at exit)", eof == 0);

    close(fds[0]);
    waitpid(pid, NULL, 0);
}

void run_test_zombie_fd(us_test_group_t *group) {
    ut_group_begin(group, "zombie_fd");
    test_zombie_pipe_eof(group);
    ut_group_end(group);
}
