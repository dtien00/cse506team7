#include "us_test_common.h"
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>

static void test_getrlimit_nofile(us_test_group_t *tg) {
    struct rlimit rl;
    int r = getrlimit(RLIMIT_NOFILE, &rl);
    TEST(tg, "getrlimit(RLIMIT_NOFILE) succeeds", r == 0);
    TEST(tg, "rlim_cur == OPEN_MAX", rl.rlim_cur == OPEN_MAX);
    TEST(tg, "rlim_max == OPEN_MAX", rl.rlim_max == OPEN_MAX);
}

static void test_setrlimit_lowers_nofile(us_test_group_t *tg) {
    struct rlimit rl = { .rlim_cur = 5, .rlim_max = OPEN_MAX };
    TEST(tg, "setrlimit to 5 succeeds", setrlimit(RLIMIT_NOFILE, &rl) == 0);
    /* fds 0, 1, 2 are stdin/stdout/stderr; slots 3 and 4 are usable */
    int fd3 = open("/hello.txt", O_RDONLY);
    int fd4 = open("/hello.txt", O_RDONLY);
    int fd5 = open("/hello.txt", O_RDONLY);   /* should fail: limit is 5 */
    TEST(tg, "fd3 opened within limit", fd3 >= 0);
    TEST(tg, "fd4 opened within limit", fd4 >= 0);
    TEST(tg, "fd5 rejected at soft limit", fd5 < 0);
    if (fd3 >= 0) close(fd3);
    if (fd4 >= 0) close(fd4);
    /* restore */
    rl.rlim_cur = OPEN_MAX;
    setrlimit(RLIMIT_NOFILE, &rl);
}

static void test_setrlimit_eperm_on_raise(us_test_group_t *tg) {
    struct rlimit rl = { .rlim_cur = OPEN_MAX + 1, .rlim_max = OPEN_MAX };
    int r = setrlimit(RLIMIT_NOFILE, &rl);
    TEST(tg, "soft > hard returns error", r < 0);
}

/* ── RLIMIT_STACK ─────────────────────────────────────────────────────────── */

static void test_getrlimit_stack_default(us_test_group_t *tg) {
    struct rlimit rl;
    int r = getrlimit(RLIMIT_STACK, &rl);
    TEST(tg, "getrlimit(RLIMIT_STACK) succeeds", r == 0);
    /* Default is SBUNIX_STACK_MAX = 256 * 4096 = 1 MB */
    TEST(tg, "rlim_cur == 1 MB default", rl.rlim_cur == 256UL * 4096UL);
    TEST(tg, "rlim_max == 1 MB default", rl.rlim_max == 256UL * 4096UL);
}

/*
 * Stack-overflow enforcement test.
 *
 * In a child: lower RLIMIT_STACK to 64 KB, then RECURSE with 512-byte frames.
 * 300 levels × ~600 B per frame ≈ 180 KB > 64 KB — the GROWSDOWN fault
 * handler will reject new growth once the stack exceeds the soft limit and
 * deliver SIGSEGV to the child.
 *
 * IMPORTANT: this must be genuine recursion, not a loop.  A loop reuses the
 * same stack frame on every iteration; no new stack pages are faulted in, so
 * the GROWSDOWN enforcement path is never triggered.
 *
 * Note: the initial stack pages already mapped for the child are NOT unmapped
 * when the limit is lowered — only *new* growth faults are checked.
 * Recursion eventually grows the stack below the existing pages, crossing the
 * rlim_floor boundary and causing kill_current(SIGSEGV).
 */
__attribute__((noinline))
static void do_stack_recurse(volatile int n) {
    volatile char buf[512];
    buf[0]   = (char)n;
    buf[511] = (char)n;
    if (n < 400)
        do_stack_recurse(n + 1);
    /* Touch buf AFTER the recursive call to prevent tail-call elimination. */
    (void)buf[0];
    (void)buf[511];
}

static void stack_overflow_child(void) {
    struct rlimit rl;
    getrlimit(RLIMIT_STACK, &rl);
    rl.rlim_cur = 64 * 1024;   /* lower soft limit to 64 KB */
    setrlimit(RLIMIT_STACK, &rl);

    /* This recursive call should trigger SIGSEGV before it returns. */
    do_stack_recurse(0);
    _exit(0);   /* should NOT be reached if enforcement is working */
}

static void test_rlimit_stack_enforced(us_test_group_t *tg) {
    pid_t child = fork();
    if (child == 0) {
        stack_overflow_child();
        _exit(0);
    }
    if (child < 0) {
        SKIP(tg, "RLIMIT_STACK enforced: fork failed", "fork unavailable");
        return;
    }
    int status = 0;
    waitpid(child, &status, 0);
    TEST(tg, "RLIMIT_STACK: child killed by signal (not clean exit)",
         WIFSIGNALED(status));
    TEST(tg, "RLIMIT_STACK: killing signal is SIGSEGV",
         WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
}

void run_test_rlimit(us_test_group_t *group) {
    ut_group_begin(group, "rlimit");
    test_getrlimit_nofile(group);
    test_setrlimit_lowers_nofile(group);
    test_setrlimit_eperm_on_raise(group);
    test_getrlimit_stack_default(group);
    test_rlimit_stack_enforced(group);
    ut_group_end(group);
}