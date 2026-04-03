#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "us_test_common.h"

void run_test_malloc(us_test_group_t *group) {
    char *p;
    char *q;
    size_t i;

    ut_group_begin(group, "malloc.h");

    p = (char *)malloc(64);
    TEST(group, "malloc returns non-null", p != 0);
    if (p != 0) {
        memset(p, 'A', 64);
        TEST(group, "malloc region writable", p[0] == 'A' && p[63] == 'A');
        free(p);
    }

    p = (char *)calloc(16, 1);
    TEST(group, "calloc returns non-null", p != 0);
    if (p != 0) {
        int zero = 1;
        for (i = 0; i < 16; i++) {
            if (p[i] != 0) {
                zero = 0;
                break;
            }
        }
        TEST(group, "calloc zero-initializes", zero);
    }

    q = (char *)realloc(p, 64);
    TEST(group, "realloc grows allocation", q != 0);
    if (q != 0) {
        TEST(group, "realloc preserves prefix", q[0] == 0 && q[1] == 0);
        free(q);
    } else if (p != 0) {
        free(p);
    }

    ut_group_end(group);
}
