#include "us_test_common.h"
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

static void test_sysconf_open_max(us_test_group_t *tg) {
    long v = sysconf(_SC_OPEN_MAX);
    TEST(tg, "sysconf(_SC_OPEN_MAX) >= 20", v >= 20);
    TEST(tg, "sysconf(_SC_OPEN_MAX) matches OPEN_MAX", v == OPEN_MAX);
}

static void test_sysconf_pagesize(us_test_group_t *tg) {
    long v = sysconf(_SC_PAGESIZE);
    TEST(tg, "sysconf(_SC_PAGESIZE) == 4096", v == 4096);
}

static void test_sysconf_clk_tck(us_test_group_t *tg) {
    long v = sysconf(_SC_CLK_TCK);
    TEST(tg, "sysconf(_SC_CLK_TCK) > 0", v > 0);
}

static void test_open_max_fds(us_test_group_t *tg) {
    /* Open OPEN_MAX - 3 new fds (0/1/2 already used). Should succeed. */
    int fds[OPEN_MAX];
    int count = 0;
    for (int i = 0; i < OPEN_MAX - 3; i++) {
        fds[i] = open("/hello.txt", O_RDONLY);
        if (fds[i] < 0) break;
        count++;
    }
    for (int i = 0; i < count; i++) close(fds[i]);
    TEST(tg, "can open OPEN_MAX-3 fds simultaneously", count == OPEN_MAX - 3);
}

void run_test_limits(us_test_group_t *group) {
    ut_group_begin(group, "sysconf_limits");
    test_sysconf_open_max(group);
    test_sysconf_pagesize(group);
    test_sysconf_clk_tck(group);
    test_open_max_fds(group);
    ut_group_end(group);
}
