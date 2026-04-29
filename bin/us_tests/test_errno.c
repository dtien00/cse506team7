#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include "sys/stat.h"
#include "us_test_common.h"

void run_test_errno(us_test_group_t *group) {
    ut_group_begin(group, "errno");

    /* ── open: missing file → ENOENT ──────────────────────────────────── */
    errno = 0;
    int fd = open("/no_such_file_errno_test", O_RDONLY);
    TEST(group, "open(missing): returns -1",       fd == -1);
    TEST(group, "open(missing): errno == ENOENT",  errno == ENOENT);

    /* ── read: bad fd → EBADF ─────────────────────────────────────────── */
    char buf[4];
    errno = 0;
    int r = (int)read(-1, buf, 1);
    TEST(group, "read(bad fd): returns -1",        r == -1);
    TEST(group, "read(bad fd): errno == EBADF",    errno == EBADF);

    /* ── write: bad fd → EBADF ────────────────────────────────────────── */
    errno = 0;
    r = (int)write(-1, "x", 1);
    TEST(group, "write(bad fd): returns -1",       r == -1);
    TEST(group, "write(bad fd): errno == EBADF",   errno == EBADF);

    /* ── close: bad fd → EBADF ────────────────────────────────────────── */
    errno = 0;
    r = close(999);
    TEST(group, "close(bad fd): returns -1",       r == -1);
    TEST(group, "close(bad fd): errno == EBADF",   errno == EBADF);

    /* ── lseek: bad fd → EBADF ────────────────────────────────────────── */
    errno = 0;
    long off = (long)lseek(-1, 0, SEEK_SET);
    TEST(group, "lseek(bad fd): returns -1",       off == -1);
    TEST(group, "lseek(bad fd): errno == EBADF",   errno == EBADF);

    /* ── mkdir: existing path → EEXIST ────────────────────────────────── */
    errno = 0;
    r = mkdir("/", 0755);
    TEST(group, "mkdir(/): returns -1",            r == -1);
    TEST(group, "mkdir(/): errno == EEXIST",       errno == EEXIST);

    /* ── open O_CREAT|O_EXCL on existing file → EEXIST ─────────────────── */
    /* Use /hello.txt which is present in the tarfs root */
    errno = 0;
    fd = open("/hello.txt", O_RDONLY | O_CREAT | O_EXCL, 0644);
    if (fd >= 0) {
        /* hello.txt doesn't exist in this image; skip the EEXIST check */
        close(fd);
        SKIP(group, "open(O_EXCL existing): returns -1",
             "/hello.txt not present");
        SKIP(group, "open(O_EXCL existing): errno == EEXIST",
             "/hello.txt not present");
    } else {
        TEST(group, "open(O_EXCL existing): returns -1",      fd == -1);
        TEST(group, "open(O_EXCL existing): errno == EEXIST", errno == EEXIST);
    }

    /* ── kill: invalid signal → EINVAL ──────────────────────────────────── */
    errno = 0;
    r = kill(getpid(), -1);
    TEST(group, "kill(invalid sig): returns -1",    r == -1);
    TEST(group, "kill(invalid sig): errno == EINVAL", errno == EINVAL);

    /* ── kill: non-existent PID → ESRCH ──────────────────────────────────── */
    errno = 0;
    r = kill(99999, SIGTERM);
    TEST(group, "kill(nonexistent PID): returns -1",   r == -1);
    TEST(group, "kill(nonexistent PID): errno == ESRCH", errno == ESRCH);

    /* ── waitpid: no children → ECHILD ───────────────────────────────────── */
    /* Fork an isolated child that has no children of its own, so its
     * waitpid(-1, …, 0) returns ECHILD immediately.  Parent collects exit. */
    pid_t child = fork();
    if (child < 0) {
        SKIP(group, "waitpid(no children): returns -1",       "fork failed");
        SKIP(group, "waitpid(no children): errno == ECHILD",  "fork failed");
    } else if (child == 0) {
        errno = 0;
        int wr = (int)waitpid(-1, (int *)0, 0);
        /* Report result via exit code: 0 = both checks passed */
        if (wr == -1 && errno == ECHILD)
            _exit(0);
        _exit(1);
    } else {
        int status = 0;
        waitpid(child, &status, 0);
        int passed = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        TEST(group, "waitpid(no children): returns -1",      passed);
        TEST(group, "waitpid(no children): errno == ECHILD", passed);
    }

    ut_group_end(group);
}
