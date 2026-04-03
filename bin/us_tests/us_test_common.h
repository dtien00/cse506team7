#ifndef US_TEST_COMMON_H
#define US_TEST_COMMON_H

#include <stdio.h>

typedef struct us_test_group {
    const char *name;
    int passed;
    int failed;
    int skipped;
} us_test_group_t;

void ut_suite_begin(const char *suite_name);
void ut_group_begin(us_test_group_t *group, const char *name);
void ut_group_end(const us_test_group_t *group);
void ut_check(us_test_group_t *group, int cond, const char *name, const char *file, int line);
void ut_skip(us_test_group_t *group, const char *name, const char *reason);
int ut_suite_end(const us_test_group_t *groups, int count);

#define TEST(group, name, cond) \
    ut_check((group), (cond), (name), __FILE__, __LINE__)

#define SKIP(group, name, reason) \
    ut_skip((group), (name), (reason))

#endif

