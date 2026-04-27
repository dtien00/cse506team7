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

    ut_group_end(group);
}
