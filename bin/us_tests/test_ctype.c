#include <ctype.h>
#include "us_test_common.h"

void run_test_ctype(us_test_group_t *group) {
    ut_group_begin(group, "ctype.h");

    TEST(group, "isalpha accepts letters", isalpha('Z') != 0);
    TEST(group, "isdigit accepts digits", isdigit('7') != 0);
    TEST(group, "isalnum rejects punctuation", isalnum('#') == 0);
    TEST(group, "isspace accepts newline", isspace('\n') != 0);
    TEST(group, "tolower converts uppercase", tolower('Q') == 'q');
    TEST(group, "toupper converts lowercase", toupper('m') == 'M');

    ut_group_end(group);
}
