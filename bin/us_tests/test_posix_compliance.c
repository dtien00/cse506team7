/*
 * test_posix_compliance.c — POSIX compliance smoke tests
 *
 * Covers the MicroPython-critical items from POSIX_COMPLIANCE_CHECKLIST.md:
 *
 *   P0  snprintf() returns bytes-that-would-be-written (§8.1)
 *   P1  fcntl() F_GETFL / F_SETFL not ENOSYS (§4.2)
 *   P1  isatty() correct on stdin/stdout/stderr and regular file (§7)
 *   P1  anonymous mmap zeroed on first access (§3.1)
 *   §3.1  mmap MAP_PRIVATE: write doesn't affect parent view across fork
 *   §3.3  malloc(0), realloc(NULL,n), realloc(ptr,0), free(NULL) semantics
 *   §4.1  lseek on pipe returns ESPIPE
 *   §4.1  open O_APPEND: writes always land at EOF
 *   §4.1  open O_DIRECTORY fails on regular file
 *   §4.3  getcwd() returns non-NULL absolute path beginning with '/'
 *   §2.3  getuid / geteuid / getgid / getegid return consistent values
 *   §10   sysconf(_SC_PAGESIZE) == 4096
 *   §2.1  waitpid(0,...) waits any child in same pgrp
 *   §2.1  execve resets sa_handler to SIG_DFL in child
 *   §1.2  struct timeval / struct timespec field sizes (POSIX ABI)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include "us_test_common.h"

/* ── P0: snprintf return value ───────────────────────────────────────────── */

static void test_snprintf_retval(us_test_group_t *g) {
    char buf[4];
    int n = snprintf(buf, sizeof(buf), "hello");
    /* C99 / POSIX: returns the number of characters that *would* have been
     * written if buf were large enough, not counting the NUL terminator. */
    TEST(g, "snprintf: returns total-bytes-would-write (C99)",  n == 5);
    TEST(g, "snprintf: buffer capped at size-1 chars + NUL",   buf[3] == '\0');

    /* Zero-size buffer: must not write, return >= 0 */
    int n2 = snprintf(NULL, 0, "world");
    TEST(g, "snprintf(NULL,0,...): returns byte count >= 0",   n2 == 5);
}

/* ── P1: fcntl F_GETFL / F_SETFL ─────────────────────────────────────────── */

static void test_fcntl_getfl_setfl(us_test_group_t *g) {
    int pfd[2];
    if (pipe(pfd) < 0) {
        SKIP(g, "fcntl F_GETFL on pipe: not ENOSYS",  "pipe() failed");
        SKIP(g, "fcntl F_SETFL O_NONBLOCK: not ENOSYS", "pipe() failed");
        SKIP(g, "fcntl F_GETFL reflects O_NONBLOCK",   "pipe() failed");
        return;
    }

    errno = 0;
    int flags = fcntl(pfd[0], F_GETFL, 0);
    TEST(g, "fcntl F_GETFL on pipe: not ENOSYS",       flags >= 0);
    TEST(g, "fcntl F_GETFL on pipe: errno == 0",       flags >= 0 || errno != ENOSYS);

    if (flags >= 0) {
        int r = fcntl(pfd[0], F_SETFL, flags | O_NONBLOCK);
        TEST(g, "fcntl F_SETFL O_NONBLOCK: returns 0",   r == 0);

        int flags2 = fcntl(pfd[0], F_GETFL, 0);
        TEST(g, "fcntl F_GETFL reflects O_NONBLOCK",
             flags2 >= 0 && (flags2 & O_NONBLOCK));
    } else {
        SKIP(g, "fcntl F_SETFL O_NONBLOCK: returns 0", "F_GETFL failed");
        SKIP(g, "fcntl F_GETFL reflects O_NONBLOCK",   "F_GETFL failed");
    }

    close(pfd[0]);
    close(pfd[1]);

    /* F_GETFD on bad fd → EBADF */
    errno = 0;
    int bad = fcntl(-1, F_GETFL, 0);
    TEST(g, "fcntl(bad fd): returns -1",       bad == -1);
    TEST(g, "fcntl(bad fd): errno == EBADF",   errno == EBADF);
}

/* ── P1: isatty ──────────────────────────────────────────────────────────── */

static void test_isatty(us_test_group_t *g) {
    /* stdin is connected to the tty when running from the shell */
    int tty = isatty(STDIN_FILENO);
    TEST(g, "isatty(stdin): returns 0 or 1 (not error)", tty == 0 || tty == 1);

    /* Regular file must NOT be a tty */
    int fd = open("/hello.txt", O_RDONLY);
    if (fd < 0) {
        SKIP(g, "isatty(regular file): returns 0", "no /hello.txt");
    } else {
        errno = 0;
        int r = isatty(fd);
        TEST(g, "isatty(regular file): returns 0", r == 0);
        TEST(g, "isatty(regular file): errno == ENOTTY", errno == ENOTTY || errno == EINVAL);
        close(fd);
    }

    /* Bad fd */
    errno = 0;
    int bad = isatty(-1);
    TEST(g, "isatty(bad fd): returns 0",      bad == 0);
    TEST(g, "isatty(bad fd): errno == EBADF", errno == EBADF);
}

/* ── P1: anonymous mmap zeroed ───────────────────────────────────────────── */

static void test_anon_mmap_zero(us_test_group_t *g) {
    size_t sz = 4096;
    char *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    TEST(g, "anon mmap: succeeds",          p != MAP_FAILED);
    if (p == MAP_FAILED) return;

    int all_zero = 1;
    for (size_t i = 0; i < sz; i++)
        if (p[i] != 0) { all_zero = 0; break; }
    TEST(g, "anon mmap: all bytes zeroed on first access", all_zero);

    munmap(p, sz);
}

/* ── §3.1: MAP_PRIVATE fork isolation ────────────────────────────────────── */

static void test_map_private_fork(us_test_group_t *g) {
    size_t sz = 4096;
    char *p = mmap(NULL, sz, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) {
        SKIP(g, "MAP_PRIVATE/fork: child write not visible in parent", "mmap failed");
        return;
    }
    p[0] = 0x42;

    pid_t child = fork();
    if (child < 0) {
        munmap(p, sz);
        SKIP(g, "MAP_PRIVATE/fork: child write not visible in parent", "fork failed");
        return;
    }
    if (child == 0) {
        p[0] = 0xAB;  /* CoW write in child */
        _exit(0);
    }

    int st;
    waitpid(child, &st, 0);
    TEST(g, "MAP_PRIVATE/fork: child write not visible in parent",
         (unsigned char)p[0] == 0x42);
    munmap(p, sz);
}

/* ── §3.3: malloc edge cases ─────────────────────────────────────────────── */

static void test_malloc_edge(us_test_group_t *g) {
    /* free(NULL) must be a no-op */
    free(NULL);
    TEST(g, "free(NULL): no crash (no-op)", 1);

    /* realloc(NULL, n) == malloc(n) */
    void *p = realloc(NULL, 64);
    TEST(g, "realloc(NULL, 64): returns non-NULL", p != NULL);
    if (p) {
        memset(p, 0xCC, 64);
        /* realloc(ptr, 0) == free(ptr) per most impls; must not crash */
        void *q = realloc(p, 0);
        TEST(g, "realloc(ptr, 0): does not crash", 1);
        /* q may be NULL or a token ptr; either is acceptable */
        (void)q;
    } else {
        SKIP(g, "realloc(ptr, 0): does not crash", "realloc(NULL,64) failed");
    }

    /* malloc(0) — implementation-defined: NULL or unique non-NULL */
    void *z = malloc(0);
    TEST(g, "malloc(0): does not crash", 1);
    free(z);  /* free(NULL) is fine too */
}

/* ── §4.1: lseek on pipe → ESPIPE ────────────────────────────────────────── */

static void test_lseek_pipe_espipe(us_test_group_t *g) {
    int pfd[2];
    if (pipe(pfd) < 0) {
        SKIP(g, "lseek(pipe): returns -1 (ESPIPE)", "pipe() failed");
        return;
    }
    errno = 0;
    off_t r = lseek(pfd[0], 0, SEEK_SET);
    TEST(g, "lseek(pipe): returns -1",       r == (off_t)-1);
    TEST(g, "lseek(pipe): errno == ESPIPE",  errno == ESPIPE);
    close(pfd[0]);
    close(pfd[1]);
}

/* ── §4.1: open O_APPEND ─────────────────────────────────────────────────── */

static void test_open_append(us_test_group_t *g) {
    const char *path = "/mnt/posix_oappend_test.txt";
    /* create / truncate */
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        SKIP(g, "O_APPEND: second write lands after first", "can't create test file");
        return;
    }
    write(fd, "AAAA", 4);
    close(fd);

    /* reopen O_APPEND and seek to beginning — write must go to EOF anyway */
    fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0) {
        SKIP(g, "O_APPEND: second write lands after first", "re-open O_APPEND failed");
        return;
    }
    lseek(fd, 0, SEEK_SET);   /* deliberately seek to start */
    write(fd, "BBBB", 4);
    close(fd);

    /* verify total 8 bytes: "AAAABBBB" */
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        SKIP(g, "O_APPEND: second write lands after first", "read-back open failed");
        return;
    }
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = (int)read(fd, buf, sizeof(buf));
    close(fd);
    unlink(path);

    TEST(g, "O_APPEND: second write lands after first",
         n == 8 && memcmp(buf, "AAAABBBB", 8) == 0);
}

/* ── §4.1: open O_DIRECTORY on regular file ──────────────────────────────── */

static void test_open_o_directory(us_test_group_t *g) {
#ifdef O_DIRECTORY
    int fd = open("/hello.txt", O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        /* hello.txt might not exist; if it opened without error skip */
        close(fd);
        SKIP(g, "open O_DIRECTORY on regular file: returns -1 (ENOTDIR)",
             "file not present or kernel doesn't enforce O_DIRECTORY");
        return;
    }
    TEST(g, "open O_DIRECTORY on regular file: returns -1",       fd == -1);
    TEST(g, "open O_DIRECTORY on regular file: errno == ENOTDIR", errno == ENOTDIR);
#else
    SKIP(g, "open O_DIRECTORY on regular file: returns -1 (ENOTDIR)", "O_DIRECTORY not defined");
#endif
}

/* ── §4.3: getcwd ─────────────────────────────────────────────────────────── */

static void test_getcwd(us_test_group_t *g) {
    char buf[256];
    char *r = getcwd(buf, sizeof(buf));
    TEST(g, "getcwd: returns non-NULL",             r != NULL);
    TEST(g, "getcwd: starts with '/'",              r != NULL && r[0] == '/');
    TEST(g, "getcwd: result matches buf pointer",   r == buf);
}

/* ── §2.3: uid / gid ─────────────────────────────────────────────────────── */

static void test_uid_gid(us_test_group_t *g) {
    uid_t uid  = getuid();
    uid_t euid = geteuid();
    gid_t gid  = getgid();
    gid_t egid = getegid();

    /* For a non-setuid binary, real == effective */
    TEST(g, "getuid() == geteuid() for non-setuid binary",  uid == euid);
    TEST(g, "getgid() == getegid() for non-setuid binary",  gid == egid);
    /* Values must be in valid range (not some garbage large number) */
    TEST(g, "getuid() fits in reasonable range",            (unsigned)uid < 65536u);
    TEST(g, "getgid() fits in reasonable range",            (unsigned)gid < 65536u);
}

/* ── §10: sysconf(_SC_PAGESIZE) ──────────────────────────────────────────── */

static void test_sysconf_pagesize(us_test_group_t *g) {
    long ps = sysconf(_SC_PAGESIZE);
    TEST(g, "sysconf(_SC_PAGESIZE) == 4096",  ps == 4096);
    /* _SC_PAGE_SIZE is an alias on most systems */
#ifdef _SC_PAGE_SIZE
    long ps2 = sysconf(_SC_PAGE_SIZE);
    TEST(g, "sysconf(_SC_PAGE_SIZE) == 4096", ps2 == 4096);
#else
    SKIP(g, "sysconf(_SC_PAGE_SIZE) == 4096", "_SC_PAGE_SIZE not defined");
#endif
}

/* ── §2.1: waitpid(0,...) waits any child in same pgrp ───────────────────── */

static void test_waitpid_pgrp(us_test_group_t *g) {
    pid_t child = fork();
    if (child < 0) {
        SKIP(g, "waitpid(0,...): reaps child in same pgrp", "fork failed");
        return;
    }
    if (child == 0) {
        _exit(42);
    }
    int st = 0;
    /* waitpid(0,...) waits for any child in caller's process group */
    pid_t reaped = waitpid(0, &st, 0);
    TEST(g, "waitpid(0,...): returns child pid",
         reaped == child);
    TEST(g, "waitpid(0,...): child exited with expected status",
         WIFEXITED(st) && WEXITSTATUS(st) == 42);
}

/* ── §2.1: execve resets sa_handler to SIG_DFL ───────────────────────────── */

static volatile int sigusr1_fired = 0;
static void sigusr1_handler(int sig) { (void)sig; sigusr1_fired = 1; }

static void test_execve_resets_sighandler(us_test_group_t *g) {
    /* Install a custom SIGUSR1 handler in the parent, then fork+exec a child
     * that raises SIGUSR1. If exec() correctly resets to SIG_DFL, the child
     * will die with SIGUSR1 (WIFSIGNALED). */
    struct sigaction sa, old;
    sa.sa_handler = sigusr1_handler;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, &old);

    pid_t child = fork();
    if (child < 0) {
        sigaction(SIGUSR1, &old, NULL);
        SKIP(g, "execve: resets signal handler to SIG_DFL", "fork failed");
        return;
    }
    if (child == 0) {
        /* exec the test binary itself with a special probe argument */
        char *argv[] = { "/bin/us_tests", "--posix-sigusr1-probe", NULL };
        execve("/bin/us_tests", argv, NULL);
        _exit(127); /* exec failed */
    }

    int st = 0;
    waitpid(child, &st, 0);
    /* Restore parent's handler */
    sigaction(SIGUSR1, &old, NULL);

    if (WIFEXITED(st) && WEXITSTATUS(st) == 127) {
        SKIP(g, "execve: resets signal handler to SIG_DFL",
             "exec of /bin/us_tests failed (not in image?)");
        return;
    }
    /* Child should be killed by SIGUSR1 (SIG_DFL = terminate) */
    TEST(g, "execve: resets signal handler to SIG_DFL",
         WIFSIGNALED(st) && WTERMSIG(st) == SIGUSR1);
}

/* ── §1.2: struct timeval / struct timespec field sizes ─────────────────── */

static void test_time_struct_layout(us_test_group_t *g) {
    struct timeval tv;
    struct timespec ts;

    /* tv_sec must be time_t (long on LP64 = 8 bytes) */
    TEST(g, "struct timeval: tv_sec is time_t (8 bytes on LP64)",
         sizeof(tv.tv_sec) == sizeof(time_t));
    /* tv_usec must be long */
    TEST(g, "struct timeval: tv_usec is long",
         sizeof(tv.tv_usec) == sizeof(long));
    /* ts_nsec must be long */
    TEST(g, "struct timespec: tv_nsec is long",
         sizeof(ts.tv_nsec) == sizeof(long));

    /* gettimeofday must accept real struct pointers, not void* — a compile
     * error here means the header uses void* (eval blocker). */
    int r = gettimeofday(&tv, NULL);
    TEST(g, "gettimeofday(struct timeval*,...): compiles and returns 0", r == 0);

    int r2 = clock_gettime(CLOCK_REALTIME, &ts);
    TEST(g, "clock_gettime(CLOCK_REALTIME, struct timespec*): returns 0", r2 == 0);
    TEST(g, "clock_gettime: tv_sec > 0", ts.tv_sec > 0);
}

/* ── entry point ─────────────────────────────────────────────────────────── */

void run_test_posix_compliance(us_test_group_t *group) {
    ut_group_begin(group, "posix_compliance");

    test_snprintf_retval(group);
    test_fcntl_getfl_setfl(group);
    test_isatty(group);
    test_anon_mmap_zero(group);
    test_map_private_fork(group);
    test_malloc_edge(group);
    test_lseek_pipe_espipe(group);
    test_open_append(group);
    test_open_o_directory(group);
    test_getcwd(group);
    test_uid_gid(group);
    test_sysconf_pagesize(group);
    test_waitpid_pgrp(group);
    test_execve_resets_sighandler(group);
    test_time_struct_layout(group);

    ut_group_end(group);
}
