#include <string.h>
#include <unistd.h>
#include "us_test_common.h"

void run_test_write(us_test_group_t *group) {
    const char *msg = "[write] smoke output\n";
    int n;

    ut_group_begin(group, "write(2)");

    n = (int)write(STDOUT_FILENO, msg, strlen(msg));
    TEST(group, "write stdout writes full length", n == (int)strlen(msg));

    n = (int)write(-1, msg, 1);
    TEST(group, "write invalid fd fails", n < 0);

    ut_group_end(group);
}
