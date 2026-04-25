/*
 * test_inode_openunlink.c — Open-then-unlink / inode ref-count correctness.
 *
 * POSIX: unlink() removes the directory entry but the inode (and its data)
 * lives until the last open fd referencing it is closed.  Tests:
 *
 *  1. Basic: open file, unlink, fd still readable, close → inode freed.
 *  2. Write after unlink: data written after unlink is readable via fd.
 *  3. Fork with open-unlinked fd: child inherits fd, reads correctly.
 *  4. Two fds, one unlinked: second fd still works.
 *  5. Unlinked file is not visible via path after unlink.
 *  6. Multiple opens + unlink: all fds keep working.
 *  7. Directory entry count: parent dir link count unchanged after inode
 *     freed (stat on parent still succeeds).
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include "us_test_common.h"

/* ── 1. basic open-then-unlink ──────────────────────────────────────────── */

static void test_basic_openunlink(us_test_group_t *group) {
    const char *path = "/mnt/xv6_ou_basic.txt";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "open-unlink-basic: create", fd >= 0);
    if (fd < 0) return;
    write(fd, "hello", 5);
    close(fd);

    /* open for reading, then unlink */
    fd = open(path, O_RDONLY);
    TEST(group, "open-unlink-basic: reopen", fd >= 0);
    if (fd < 0) return;

    int r = unlink(path);
    TEST(group, "open-unlink-basic: unlink returns 0", r == 0);

    /* path is gone */
    struct stat st;
    TEST(group, "open-unlink-basic: path invisible after unlink",
         stat(path, &st) < 0);

    /* fd still readable */
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = (int)read(fd, buf, sizeof(buf));
    TEST(group, "open-unlink-basic: fd still readable after unlink", n == 5);
    TEST(group, "open-unlink-basic: data intact", memcmp(buf, "hello", 5) == 0);
    close(fd);

    /* after close, any re-open of path fails */
    TEST(group, "open-unlink-basic: path still gone after fd closed",
         open(path, O_RDONLY) < 0);
}

/* ── 2. write after unlink ──────────────────────────────────────────────── */

static void test_write_after_unlink(us_test_group_t *group) {
    const char *path = "/mnt/xv6_ou_write.txt";

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST(group, "write-after-unlink: create rdwr", fd >= 0);
    if (fd < 0) return;

    unlink(path); /* unlink while fd open */

    /* now write through fd */
    write(fd, "postunlink", 10);

    /* seek back and read */
    lseek(fd, 0, SEEK_SET);
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = (int)read(fd, buf, sizeof(buf));
    TEST(group, "write-after-unlink: write+read after unlink", n == 10);
    TEST(group, "write-after-unlink: data correct",
         memcmp(buf, "postunlink", 10) == 0);
    close(fd);
}

/* ── 3. fork inherits open-unlinked fd ───────────────────────────────────── */

static void test_fork_openunlink(us_test_group_t *group) {
    const char *path = "/mnt/xv6_ou_fork.txt";

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST(group, "fork-open-unlink: create", fd >= 0);
    if (fd < 0) return;
    write(fd, "forkdata", 8);
    lseek(fd, 0, SEEK_SET);
    unlink(path);

    pid_t pid = fork();
    if (pid == 0) {
        char buf[16];
        memset(buf, 0, sizeof(buf));
        int n = (int)read(fd, buf, sizeof(buf));
        /* exit 0 if read worked, 1 otherwise */
        _exit(n == 8 && memcmp(buf, "forkdata", 8) == 0 ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "fork-open-unlink: child reads unlinked fd correctly",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(fd);
}

/* ── 4. two fds, one unlinked: second still works ───────────────────────── */

static void test_two_fds_unlink(us_test_group_t *group) {
    const char *path = "/mnt/xv6_ou_twofds.txt";

    int fd1 = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST(group, "two-fds-unlink: create fd1", fd1 >= 0);
    if (fd1 < 0) return;
    write(fd1, "twofds", 6);

    int fd2 = open(path, O_RDONLY);
    TEST(group, "two-fds-unlink: open fd2", fd2 >= 0);
    if (fd2 < 0) { close(fd1); return; }

    unlink(path); /* remove dir entry; inode has ref from fd1 + fd2 */

    /* fd2 should still read */
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = (int)read(fd2, buf, sizeof(buf));
    TEST(group, "two-fds-unlink: fd2 readable after unlink", n == 6);
    TEST(group, "two-fds-unlink: fd2 data correct", memcmp(buf, "twofds", 6) == 0);

    close(fd2);
    close(fd1);
}

/* ── 5. multiple opens + unlink: all fds keep working ───────────────────── */

static void test_multi_open_unlink(us_test_group_t *group) {
#define N_FDS 4
    const char *path = "/mnt/xv6_ou_multi.txt";

    int fd0 = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    TEST(group, "multi-open-unlink: create", fd0 >= 0);
    if (fd0 < 0) return;
    write(fd0, "multiopen", 9);

    int fds[N_FDS];
    fds[0] = fd0;
    for (int i = 1; i < N_FDS; i++) {
        fds[i] = open(path, O_RDONLY);
        TEST(group, "multi-open-unlink: open extra fd", fds[i] >= 0);
    }

    unlink(path);

    /* all fds should still be readable */
    int all_ok = 1;
    for (int i = 0; i < N_FDS; i++) {
        if (fds[i] < 0) continue;
        lseek(fds[i], 0, SEEK_SET);
        char buf[16];
        memset(buf, 0, sizeof(buf));
        int n = (int)read(fds[i], buf, sizeof(buf));
        if (n != 9 || memcmp(buf, "multiopen", 9) != 0) all_ok = 0;
    }
    TEST(group, "multi-open-unlink: all 4 fds readable after unlink", all_ok);

    for (int i = 0; i < N_FDS; i++)
        if (fds[i] >= 0) close(fds[i]);
#undef N_FDS
}

/* ── 6. parent dir still valid after inode freed ─────────────────────────── */

static void test_dir_after_inode_free(us_test_group_t *group) {
    const char *path = "/mnt/xv6_ou_dir_check.txt";

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "dir-after-free: create file", fd >= 0);
    if (fd >= 0) {
        unlink(path);
        close(fd); /* inode freed here */
    }

    /* /mnt must still be stat-able */
    struct stat st;
    int r = stat("/mnt", &st);
    TEST(group, "dir-after-free: /mnt stat succeeds after inode freed", r == 0);
    TEST(group, "dir-after-free: /mnt is still a directory",
         r == 0 && S_ISDIR(st.st_mode));

    /* can create new file in /mnt (free slot reused) */
    fd = open("/mnt/xv6_ou_after_free.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    TEST(group, "dir-after-free: new file creation in /mnt succeeds", fd >= 0);
    if (fd >= 0) {
        close(fd);
        unlink("/mnt/xv6_ou_after_free.txt");
    }
}

/* ── public entry point ─────────────────────────────────────────────────── */

void run_test_inode_openunlink(us_test_group_t *group) {
    ut_group_begin(group, "inode_openunlink");

    test_basic_openunlink(group);
    test_write_after_unlink(group);
    test_fork_openunlink(group);
    test_two_fds_unlink(group);
    test_multi_open_unlink(group);
    test_dir_after_inode_free(group);

    ut_group_end(group);
}
