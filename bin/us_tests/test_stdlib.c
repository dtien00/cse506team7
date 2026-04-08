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
    int dup_vals[] = {5, 3, 5, 1, 3, 2};
    char no_digits[] = "xyz";
    int key = 3;
    int missing = 6;
    int *found;
    char *end = 0;
    div_t d;
    ldiv_t ld;
    lldiv_t lld;
    int rand_a;
    int rand_b;

    ut_group_begin(group, "stdlib.h");

    TEST(group, "atoi parses decimal", atoi("1234") == 1234);
    TEST(group, "atoi handles whitespace and sign", atoi("  -42") == -42);
    TEST(group, "strtol parses hex", strtol("0x20", &end, 0) == 32 && end != 0 && *end == '\0');
    TEST(group, "strtol parses octal with base auto-detect",
         strtol("075", &end, 0) == 61 && end != 0 && *end == '\0');
    TEST(group, "strtol preserves endptr for trailing text",
         strtol("123xyz", &end, 10) == 123 && end != 0 && strcmp(end, "xyz") == 0);
    TEST(group, "strtol consumes lone zero",
         strtol("0", &end, 0) == 0 && end != 0 && *end == '\0');
    TEST(group, "strtol reports no digits",
         strtol(no_digits, &end, 10) == 0 && end == no_digits);
    TEST(group, "strtoul parses prefixed hex",
         strtoul("0x2a", &end, 0) == 42ul && end != 0 && *end == '\0');
    TEST(group, "atoll parses negative decimal", atoll("-9001") == -9001);
    TEST(group, "abs handles negative", abs(-9) == 9);
    TEST(group, "labs handles negative", labs(-123456L) == 123456L);
    TEST(group, "llabs handles negative", llabs(-123456789LL) == 123456789LL);

    d = div(17, 5);
    ld = ldiv(29L, 6L);
    lld = lldiv(123LL, 10LL);
    TEST(group, "div returns quotient and remainder", d.quot == 3 && d.rem == 2);
    TEST(group, "ldiv returns quotient and remainder", ld.quot == 4 && ld.rem == 5);
    TEST(group, "lldiv returns quotient and remainder", lld.quot == 12 && lld.rem == 3);

    srand(7);
    rand_a = rand();
    srand(7);
    rand_b = rand();
    TEST(group, "srand makes rand deterministic", rand_a == rand_b);

    qsort(vals, 4, sizeof(int), int_cmp);
    TEST(group, "qsort sorts ascending", vals[0] == 1 && vals[1] == 2 && vals[2] == 3 && vals[3] == 4);
    qsort(dup_vals, 6, sizeof(int), int_cmp);
    TEST(group, "qsort handles duplicate values",
         dup_vals[0] == 1 && dup_vals[1] == 2 && dup_vals[2] == 3 &&
         dup_vals[3] == 3 && dup_vals[4] == 5 && dup_vals[5] == 5);

    found = (int *)bsearch(&key, vals, 4, sizeof(int), int_cmp);
    TEST(group, "bsearch finds existing key", found != 0 && *found == 3);
    TEST(group, "bsearch returns null for missing key",
         bsearch(&missing, vals, 4, sizeof(int), int_cmp) == 0);

    ut_group_end(group);
}
