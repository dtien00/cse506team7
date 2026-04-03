#include <string.h>
#include "us_test_common.h"

void run_test_string(us_test_group_t *group) {
    char a[32];
    char b[32];

    ut_group_begin(group, "string.h");

    strcpy(a, "hello");
    strcpy(b, " world");
    strcat(a, b);
    TEST(group, "strcat combines strings", strcmp(a, "hello world") == 0);
    TEST(group, "strlen computes length", strlen(a) == 11);

    memset(a, 0, sizeof(a));
    memcpy(a, "abcdef", 7);
    TEST(group, "memcpy copies bytes", memcmp(a, "abcdef", 7) == 0);

    memmove(a + 2, a, 4);
    TEST(group, "memmove handles overlap", memcmp(a, "ababcd", 6) == 0);

    TEST(group, "strchr finds character", strchr("kernel", 'n') != 0);
    TEST(group, "strstr finds substring", strstr("userspace tests", "space") != 0);

    ut_group_end(group);
}
