#include "us_test_common.h"
#include <sys/resource.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

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

void run_test_rlimit(us_test_group_t *group) {
    ut_group_begin(group, "rlimit");
    test_getrlimit_nofile(group);
    test_setrlimit_lowers_nofile(group);
    test_setrlimit_eperm_on_raise(group);
    ut_group_end(group);
}