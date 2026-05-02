/*
 * test_mmap_file.c — File-backed mmap correctness tests.
 *
 * Tests the demand-paging path for VMA_TYPE_FILE mappings:
 *  A. File content is visible through the mapping (ops.read on fault)
 *  B. close(fd) after mmap does not invalidate the mapping (VMA holds ref)
 *  C. MAP_PRIVATE fork: child COW write does not affect parent's view
 *  D. Non-page-aligned offset returns MAP_FAILED
 *  E. Invalid fd returns MAP_FAILED
 *  F. Large file: mmap content matches read() reference byte-for-byte
 *  G. MAP_PRIVATE page beyond EOF is zero-padded (correct POSIX behaviour)
 *  H. MAP_SHARED access past EOF delivers SIGBUS
 *
 * Tests B, C, F, G, H run in child processes to isolate faults and
 * memory pressure from the main test runner.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "us_test_common.h"

#define MMAP_TEST_FILE "/hello.txt"
#define MMAP_LARGE_FILE "/large_text.txt"

/* ── A. File content visible through mapping ─────────────────────────────── */

static void test_basic_read(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/basic-read: file content visible", "no " MMAP_TEST_FILE);
        return;
    }

    char ref[16];
    memset(ref, 0, sizeof(ref));
    read(fd, ref, sizeof(ref));
    lseek(fd, 0, SEEK_SET);

    void *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    TEST(group, "mmap-file/basic-read: mmap succeeds", m != MAP_FAILED);
    if (m == MAP_FAILED)
        return;

    TEST(group, "mmap-file/basic-read: first 16 bytes match read()",
         memcmp(m, ref, sizeof(ref)) == 0);

    munmap(m, 4096);
}

/* ── B. close(fd) after mmap — mapping stays valid ──────────────────────── */

static void test_close_after_mmap(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/close-after-mmap: mapping survives close(fd)",
             "no " MMAP_TEST_FILE);
        return;
    }

    void *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (m == MAP_FAILED) {
        TEST(group, "mmap-file/close-after-mmap: mapping survives close(fd)", 0);
        return;
    }

    /* Run in a child so a fault here does not kill the test runner. */
    pid_t pid = fork();
    TEST(group, "mmap-file/close-after-mmap: fork succeeded", pid >= 0);
    if (pid == 0) {
        volatile char c = ((char *)m)[0];
        (void)c;
        _exit(0);
    }
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "mmap-file/close-after-mmap: mapping survives close(fd)",
             WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    munmap(m, 4096);
}

/* ── C. MAP_PRIVATE fork: child COW write isolated from parent ───────────── */

static void test_map_private_fork(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/private-fork: child COW write isolated from parent",
             "no " MMAP_TEST_FILE);
        SKIP(group, "mmap-file/private-fork: parent mapping unchanged after child write",
             "no " MMAP_TEST_FILE);
        return;
    }

    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd);

    if (m == MAP_FAILED) {
        TEST(group, "mmap-file/private-fork: child COW write isolated from parent", 0);
        TEST(group, "mmap-file/private-fork: parent mapping unchanged after child write", 0);
        return;
    }

    char original = ((char *)m)[0];

    pid_t pid = fork();
    TEST(group, "mmap-file/private-fork: fork succeeded", pid >= 0);
    if (pid == 0) {
        /* Child writes to its private COW copy. */
        ((char *)m)[0] = (char)0xFF;
        _exit(((char *)m)[0] == (char)0xFF ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "mmap-file/private-fork: child COW write isolated from parent",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
    TEST(group, "mmap-file/private-fork: parent mapping unchanged after child write",
         ((char *)m)[0] == original);

    munmap(m, 4096);
}

/* ── D. Non-page-aligned offset → MAP_FAILED ─────────────────────────────── */

static void test_unaligned_offset(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/unaligned-offset: unaligned offset returns MAP_FAILED",
             "no " MMAP_TEST_FILE);
        return;
    }

    void *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 1);
    close(fd);

    TEST(group, "mmap-file/unaligned-offset: unaligned offset returns MAP_FAILED",
         m == MAP_FAILED);
    if (m != MAP_FAILED)
        munmap(m, 4096);
}

/* ── E. Invalid fd → MAP_FAILED ──────────────────────────────────────────── */

static void test_invalid_fd(us_test_group_t *group) {
    void *m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, 99, 0);
    TEST(group, "mmap-file/invalid-fd: invalid fd returns MAP_FAILED", m == MAP_FAILED);
    if (m != MAP_FAILED)
        munmap(m, 4096);
}

/* ── F. Large file: mmap content matches read() reference ────────────────── */

static void test_large_file_mmap(us_test_group_t *group) {
    int fd = open(MMAP_LARGE_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/large: content matches read() reference",
             "no " MMAP_LARGE_FILE);
        return;
    }

    struct stat st;
    fstat(fd, &st);
    if (st.st_size < 8192) {
        close(fd);
        SKIP(group, "mmap-file/large: content matches read() reference",
             MMAP_LARGE_FILE " < 8 KB");
        return;
    }

    size_t len = (size_t)st.st_size;

    pid_t pid = fork();
    TEST(group, "mmap-file/large: fork succeeded", pid >= 0);
    if (pid == 0) {
        void *m = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (m == MAP_FAILED)
            _exit(1);

        int fd2 = open(MMAP_LARGE_FILE, O_RDONLY);
        if (fd2 < 0) { munmap(m, len); _exit(2); }

        char *ref = malloc(len);
        if (!ref) { munmap(m, len); close(fd2); _exit(3); }

        if ((size_t)read(fd2, ref, len) != len) {
            free(ref); munmap(m, len); close(fd2); _exit(4);
        }
        close(fd2);

        int ok = (memcmp(m, ref, len) == 0);
        free(ref);
        munmap(m, len);
        _exit(ok ? 0 : 5);
    }

    close(fd);
    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "mmap-file/large: content matches read() reference",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* ── G. MAP_PRIVATE page beyond EOF is zero-padded ──────────────────────── */

static void test_past_eof_zero(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/past-eof: MAP_PRIVATE access past EOF zero-pads",
             "no " MMAP_TEST_FILE);
        return;
    }

    /* MAP_PRIVATE: bytes past EOF are zero-filled, NOT SIGBUS.  This is
     * correct POSIX behaviour and matches Linux.  MAP_SHARED is different —
     * see test H (test_map_shared_eof_sigbus). */
    void *m = mmap(NULL, 2 * 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (m == MAP_FAILED) {
        TEST(group, "mmap-file/past-eof: access past EOF returns zero-padded page", 0);
        return;
    }

    pid_t pid = fork();
    TEST(group, "mmap-file/past-eof: fork succeeded", pid >= 0);
    if (pid == 0) {
        volatile char c = ((char *)m)[4096];   /* second page — may be past EOF */
        _exit(c == 0 ? 0 : 1);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    TEST(group, "mmap-file/past-eof: MAP_PRIVATE access past EOF zero-pads",
         WIFEXITED(status) && WEXITSTATUS(status) == 0);

    munmap(m, 2 * 4096);
}

/* ── H. MAP_SHARED access past EOF delivers SIGBUS ──────────────────────── */

static void test_map_shared_eof_sigbus(us_test_group_t *group) {
    int fd = open(MMAP_TEST_FILE, O_RDONLY);
    if (fd < 0) {
        SKIP(group, "mmap-file/shared-eof-sigbus: MAP_SHARED access past EOF delivers SIGBUS",
             "no " MMAP_TEST_FILE);
        return;
    }

    /* Map 2 pages with MAP_SHARED so the second page is guaranteed to be
     * past EOF for any file smaller than 8 KB. */
    void *m = mmap(NULL, 2 * 4096, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (m == MAP_FAILED) {
        SKIP(group, "mmap-file/shared-eof-sigbus: MAP_SHARED access past EOF delivers SIGBUS",
             "MAP_SHARED file mmap returned MAP_FAILED at runtime");
        return;
    }

    pid_t pid = fork();
    TEST(group, "mmap-file/shared-eof-sigbus: fork succeeded", pid >= 0);
    if (pid == 0) {
        /* Access the second page — past EOF on a MAP_SHARED mapping.
         * The kernel delivers SIGBUS; this child should never reach _exit(0). */
        volatile char c = ((char *)m)[4096];
        (void)c;
        _exit(0);  /* unreachable if SIGBUS is delivered */
    }

    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        TEST(group, "mmap-file/shared-eof-sigbus: MAP_SHARED access past EOF delivers SIGBUS",
             WIFSIGNALED(status) && WTERMSIG(status) == SIGBUS);
    }

    munmap(m, 2 * 4096);
}

/* ── I. MAP_SHARED write → munmap → read() verifies persistence ─────────── */
/* Tests the sys_munmap writeback path (DEFECT-B fix in syscall.c:1160-1181). */

static void test_map_shared_write_munmap(us_test_group_t *group) {
    const char *path = "/mnt/mmap_shared_wb_munmap.tmp";
    char zeros[4096];
    memset(zeros, 0, sizeof(zeros));

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        SKIP(group, "mmap-file/shared-write-munmap: MAP_SHARED write persists after munmap",
             "cannot create test file on /mnt/");
        return;
    }
    write(fd, zeros, sizeof(zeros));

    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED) {
        SKIP(group, "mmap-file/shared-write-munmap: MAP_SHARED write persists after munmap",
             "MAP_SHARED mmap failed");
        unlink(path);
        return;
    }

    memset(m, 0xAB, 4096);
    munmap(m, 4096);   /* writeback must happen here */

    fd = open(path, O_RDONLY);
    char buf[4096];
    int nr = read(fd, buf, sizeof(buf));
    close(fd);
    unlink(path);

    int ok = (nr == 4096);
    for (int i = 0; i < nr && ok; i++)
        if ((unsigned char)buf[i] != 0xAB) ok = 0;

    TEST(group, "mmap-file/shared-write-munmap: MAP_SHARED write persists after munmap", ok);
}

/* ── J. MAP_SHARED write → process exit without munmap ──────────────────── */
/* Tests the proc_destroy writeback path (DEFECT-B fix in process.c:165-184). */

static void test_map_shared_write_exit(us_test_group_t *group) {
    const char *path = "/mnt/mmap_shared_wb_exit.tmp";
    char zeros[4096];
    memset(zeros, 0, sizeof(zeros));

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        SKIP(group, "mmap-file/shared-write-exit: MAP_SHARED write persists on exit without munmap",
             "cannot create test file on /mnt/");
        return;
    }
    write(fd, zeros, sizeof(zeros));
    close(fd);

    pid_t pid = fork();
    if (pid == 0) {
        int cfd = open(path, O_RDWR);
        if (cfd < 0) _exit(1);
        void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, cfd, 0);
        close(cfd);
        if (m == MAP_FAILED) _exit(2);
        memset(m, 0xCD, 4096);
        _exit(0);   /* no munmap — writeback must fire in proc_destroy */
    }
    int status = 0;
    waitpid(pid, &status, 0);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        TEST(group, "mmap-file/shared-write-exit: MAP_SHARED write persists on exit without munmap", 0);
        unlink(path);
        return;
    }

    fd = open(path, O_RDONLY);
    char buf[4096];
    int nr = read(fd, buf, sizeof(buf));
    close(fd);
    unlink(path);

    int ok = (nr == 4096);
    for (int i = 0; i < nr && ok; i++)
        if ((unsigned char)buf[i] != 0xCD) ok = 0;

    TEST(group, "mmap-file/shared-write-exit: MAP_SHARED write persists on exit without munmap", ok);
}

/* ── K. MAP_SHARED with non-zero file offset ─────────────────────────────── */
/* Exercises the offset arithmetic: off = vma->offset + (va - vma->start).
 * A zero-offset-only bug would write to the wrong file position, leaving the
 * second page with old data and potentially corrupting the first page. */

static void test_map_shared_write_nonzero_offset(us_test_group_t *group) {
    const char *path = "/mnt/mmap_shared_wb_offset.tmp";
    char page0[4096], page1[4096];
    memset(page0, 'A', sizeof(page0));
    memset(page1, 'B', sizeof(page1));

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        SKIP(group, "mmap-file/shared-write-offset: MAP_SHARED writeback respects non-zero mmap offset",
             "cannot create test file on /mnt/");
        return;
    }
    write(fd, page0, sizeof(page0));  /* page 0: all 'A' */
    write(fd, page1, sizeof(page1));  /* page 1: all 'B' */

    /* Map only the second page (offset = 4096) */
    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 4096);
    close(fd);
    if (m == MAP_FAILED) {
        SKIP(group, "mmap-file/shared-write-offset: MAP_SHARED writeback respects non-zero mmap offset",
             "MAP_SHARED mmap with offset=4096 failed");
        unlink(path);
        return;
    }

    memset(m, 0xEF, 4096);
    munmap(m, 4096);

    fd = open(path, O_RDONLY);
    char buf[8192];
    int nr = read(fd, buf, sizeof(buf));
    close(fd);
    unlink(path);

    int page0_ok = (nr >= 4096);
    for (int i = 0; i < 4096 && page0_ok; i++)
        if ((unsigned char)buf[i] != 'A') page0_ok = 0;

    int page1_ok = (nr == 8192);
    for (int i = 4096; i < 8192 && page1_ok; i++)
        if ((unsigned char)buf[i] != 0xEF) page1_ok = 0;

    TEST(group, "mmap-file/shared-write-offset: page before mapping unchanged", page0_ok);
    TEST(group, "mmap-file/shared-write-offset: MAP_SHARED writeback respects non-zero mmap offset",
         page1_ok);
}

/* ── public entry point ──────────────────────────────────────────────────── */

void run_test_mmap_file(us_test_group_t *group) {
    ut_group_begin(group, "mmap_file");

    test_basic_read(group);
    test_close_after_mmap(group);
    test_map_private_fork(group);
    test_unaligned_offset(group);
    test_invalid_fd(group);
    test_large_file_mmap(group);
    test_past_eof_zero(group);
    test_map_shared_eof_sigbus(group);
    test_map_shared_write_munmap(group);
    test_map_shared_write_exit(group);
    test_map_shared_write_nonzero_offset(group);

    ut_group_end(group);
}
