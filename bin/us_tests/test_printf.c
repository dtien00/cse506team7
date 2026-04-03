#include <stdio.h>
#include <string.h>
#include "us_test_common.h"

void run_test_printf(us_test_group_t *group) {
    char buf[64];
    int n;

    ut_group_begin(group, "stdio.h");

    n = sprintf(buf, "%s %d", "abc", 42);
    TEST(group, "sprintf formats string and int", n > 0 && strcmp(buf, "abc 42") == 0);

    memset(buf, 0, sizeof(buf));
    n = snprintf(buf, 5, "%s", "abcdef");
    TEST(group, "snprintf truncates and returns full length", n == 6 && strcmp(buf, "abcd") == 0);

    TEST(group, "puts returns non-EOF", puts("[stdio] puts smoke") != EOF);
    TEST(group, "putchar returns written char", putchar('\n') == '\n');

    ut_group_end(group);
}
