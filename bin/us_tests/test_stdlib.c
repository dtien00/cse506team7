#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "us_test_common.h"

static int int_cmp(const void *lhs, const void *rhs) {
    const int *a = (const int *)lhs;
    const int *b = (const int *)rhs;
    return (*a > *b) - (*a < *b);
}

void run_test_stdlib(us_test_group_t *group) {
    int vals[] = {4, 1, 3, 2};
    int key = 3;
    int *found;
    char *end = 0;

    ut_group_begin(group, "stdlib.h");

    TEST(group, "atoi parses decimal", atoi("1234") == 1234);
    TEST(group, "strtol parses hex", strtol("0x20", &end, 0) == 32 && end != 0 && *end == '\0');
    TEST(group, "abs handles negative", abs(-9) == 9);

    qsort(vals, 4, sizeof(int), int_cmp);
    TEST(group, "qsort sorts ascending", vals[0] == 1 && vals[1] == 2 && vals[2] == 3 && vals[3] == 4);

    found = (int *)bsearch(&key, vals, 4, sizeof(int), int_cmp);
    TEST(group, "bsearch finds existing key", found != 0 && *found == 3);

    ut_group_end(group);
}
