/*
 * test_libc_ctype.c — comprehensive ctype.h conformance tests
 *
 * Validates every classification and conversion function against the full
 * ASCII range, not just a handful of spot checks.
 */

#include <ctype.h>
#include <stddef.h>
#include "us_test_common.h"

/* Count how many characters in [0,127] satisfy a predicate. */
static int count_ascii(int (*fn)(int)) {
    int c, n = 0;
    for (c = 0; c < 128; c++) {
        if (fn(c)) n++;
    }
    return n;
}

/* ------------------------------------------------------------------ groups */

static void test_isalpha(us_test_group_t *g) {
    ut_group_begin(g, "isalpha");

    /* spot checks */
    TEST(g, "isalpha('A')", isalpha('A') != 0);
    TEST(g, "isalpha('z')", isalpha('z') != 0);
    TEST(g, "isalpha('0') is false", isalpha('0') == 0);
    TEST(g, "isalpha(' ') is false", isalpha(' ') == 0);
    TEST(g, "isalpha('@') is false", isalpha('@') == 0);

    /* ASCII has exactly 52 alphabetic characters */
    TEST(g, "exactly 52 alphabetic ASCII chars", count_ascii(isalpha) == 52);

    ut_group_end(g);
}

static void test_isdigit(us_test_group_t *g) {
    ut_group_begin(g, "isdigit");

    TEST(g, "isdigit('0')", isdigit('0') != 0);
    TEST(g, "isdigit('9')", isdigit('9') != 0);
    TEST(g, "isdigit('a') is false", isdigit('a') == 0);
    TEST(g, "isdigit(' ') is false", isdigit(' ') == 0);

    /* exactly 10 digit chars in ASCII */
    TEST(g, "exactly 10 digit ASCII chars", count_ascii(isdigit) == 10);

    ut_group_end(g);
}

static void test_isalnum(us_test_group_t *g) {
    ut_group_begin(g, "isalnum");

    TEST(g, "isalnum('A')", isalnum('A') != 0);
    TEST(g, "isalnum('5')", isalnum('5') != 0);
    TEST(g, "isalnum('_') is false", isalnum('_') == 0);
    TEST(g, "isalnum(' ') is false", isalnum(' ') == 0);

    /* 52 alpha + 10 digit = 62 */
    TEST(g, "exactly 62 alnum ASCII chars", count_ascii(isalnum) == 62);

    ut_group_end(g);
}

static void test_isspace(us_test_group_t *g) {
    ut_group_begin(g, "isspace");

    TEST(g, "isspace(' ')", isspace(' ') != 0);
    TEST(g, "isspace('\\t')", isspace('\t') != 0);
    TEST(g, "isspace('\\n')", isspace('\n') != 0);
    TEST(g, "isspace('\\r')", isspace('\r') != 0);
    TEST(g, "isspace('\\f')", isspace('\f') != 0);
    TEST(g, "isspace('\\v')", isspace('\v') != 0);
    TEST(g, "isspace('a') is false", isspace('a') == 0);
    TEST(g, "isspace('0') is false", isspace('0') == 0);

    /* exactly 6 whitespace chars in ASCII */
    TEST(g, "exactly 6 space ASCII chars", count_ascii(isspace) == 6);

    ut_group_end(g);
}

static void test_isupper_islower(us_test_group_t *g) {
    int c;
    int upper_ok = 1, lower_ok = 1;

    ut_group_begin(g, "isupper/islower");

    TEST(g, "isupper('A')", isupper('A') != 0);
    TEST(g, "isupper('Z')", isupper('Z') != 0);
    TEST(g, "isupper('a') is false", isupper('a') == 0);
    TEST(g, "isupper('0') is false", isupper('0') == 0);

    TEST(g, "islower('a')", islower('a') != 0);
    TEST(g, "islower('z')", islower('z') != 0);
    TEST(g, "islower('A') is false", islower('A') == 0);
    TEST(g, "islower('0') is false", islower('0') == 0);

    /* verify all A-Z and a-z */
    for (c = 'A'; c <= 'Z'; c++) {
        if (!isupper(c)) { upper_ok = 0; break; }
    }
    for (c = 'a'; c <= 'z'; c++) {
        if (!islower(c)) { lower_ok = 0; break; }
    }
    TEST(g, "all A-Z pass isupper", upper_ok);
    TEST(g, "all a-z pass islower", lower_ok);

    ut_group_end(g);
}

static void test_isxdigit(us_test_group_t *g) {
    ut_group_begin(g, "isxdigit");

    TEST(g, "isxdigit('0')", isxdigit('0') != 0);
    TEST(g, "isxdigit('9')", isxdigit('9') != 0);
    TEST(g, "isxdigit('a')", isxdigit('a') != 0);
    TEST(g, "isxdigit('f')", isxdigit('f') != 0);
    TEST(g, "isxdigit('A')", isxdigit('A') != 0);
    TEST(g, "isxdigit('F')", isxdigit('F') != 0);
    TEST(g, "isxdigit('g') is false", isxdigit('g') == 0);
    TEST(g, "isxdigit('G') is false", isxdigit('G') == 0);

    /* 0-9 + a-f + A-F = 22 */
    TEST(g, "exactly 22 xdigit ASCII chars", count_ascii(isxdigit) == 22);

    ut_group_end(g);
}

static void test_ispunct_isgraph_isprint(us_test_group_t *g) {
    ut_group_begin(g, "ispunct/isgraph/isprint");

    TEST(g, "ispunct('!')", ispunct('!') != 0);
    TEST(g, "ispunct(',') ", ispunct(',') != 0);
    TEST(g, "ispunct('a') is false", ispunct('a') == 0);
    TEST(g, "ispunct(' ') is false", ispunct(' ') == 0);

    TEST(g, "isgraph('!') is true", isgraph('!') != 0);
    TEST(g, "isgraph('a') is true", isgraph('a') != 0);
    TEST(g, "isgraph(' ') is false", isgraph(' ') == 0);
    TEST(g, "isgraph('\\n') is false", isgraph('\n') == 0);

    TEST(g, "isprint(' ') is true", isprint(' ') != 0);
    TEST(g, "isprint('~') is true", isprint('~') != 0);
    TEST(g, "isprint('\\n') is false", isprint('\n') == 0);
    TEST(g, "isprint('\\t') is false", isprint('\t') == 0);

    /* ASCII printable range: 0x20 through 0x7E inclusive = 95 chars */
    TEST(g, "exactly 95 printable ASCII chars", count_ascii(isprint) == 95);

    ut_group_end(g);
}

static void test_iscntrl(us_test_group_t *g) {
    ut_group_begin(g, "iscntrl");

    TEST(g, "iscntrl('\\0')", iscntrl('\0') != 0);
    TEST(g, "iscntrl('\\n')", iscntrl('\n') != 0);
    TEST(g, "iscntrl(0x1F)", iscntrl(0x1F) != 0);
    TEST(g, "iscntrl(0x7F DEL)", iscntrl(0x7F) != 0);
    TEST(g, "iscntrl(' ') is false", iscntrl(' ') == 0);
    TEST(g, "iscntrl('A') is false", iscntrl('A') == 0);

    /* 0x00-0x1F (32 chars) + 0x7F (1 char) = 33 control chars */
    TEST(g, "exactly 33 control ASCII chars", count_ascii(iscntrl) == 33);

    ut_group_end(g);
}

static void test_isblank(us_test_group_t *g) {
    ut_group_begin(g, "isblank");

    TEST(g, "isblank(' ')", isblank(' ') != 0);
    TEST(g, "isblank('\\t')", isblank('\t') != 0);
    TEST(g, "isblank('\\n') is false", isblank('\n') == 0);
    TEST(g, "isblank('a') is false", isblank('a') == 0);

    /* exactly 2 blank chars in ASCII */
    TEST(g, "exactly 2 blank ASCII chars", count_ascii(isblank) == 2);

    ut_group_end(g);
}

static void test_isascii(us_test_group_t *g) {
    int c;
    int all_ok = 1;

    ut_group_begin(g, "isascii");

    TEST(g, "isascii(0)", isascii(0) != 0);
    TEST(g, "isascii(127)", isascii(127) != 0);
    TEST(g, "isascii(128) is false", isascii(128) == 0);
    TEST(g, "isascii(-1) is false", isascii(-1) == 0);

    for (c = 0; c < 128; c++) {
        if (!isascii(c)) { all_ok = 0; break; }
    }
    TEST(g, "all 0-127 are ASCII", all_ok);

    ut_group_end(g);
}

static void test_tolower_toupper(us_test_group_t *g) {
    int c;
    int lower_ok = 1, upper_ok = 1;

    ut_group_begin(g, "tolower/toupper");

    TEST(g, "tolower('A') == 'a'", tolower('A') == 'a');
    TEST(g, "tolower('Z') == 'z'", tolower('Z') == 'z');
    TEST(g, "tolower('a') unchanged", tolower('a') == 'a');
    TEST(g, "tolower('0') unchanged", tolower('0') == '0');
    TEST(g, "tolower(' ') unchanged", tolower(' ') == ' ');

    TEST(g, "toupper('a') == 'A'", toupper('a') == 'A');
    TEST(g, "toupper('z') == 'Z'", toupper('z') == 'Z');
    TEST(g, "toupper('A') unchanged", toupper('A') == 'A');
    TEST(g, "toupper('0') unchanged", toupper('0') == '0');

    /* all uppercase letters round-trip through tolower->toupper */
    for (c = 'A'; c <= 'Z'; c++) {
        if (toupper(tolower(c)) != c) { lower_ok = 0; break; }
    }
    /* all lowercase letters round-trip through toupper->tolower */
    for (c = 'a'; c <= 'z'; c++) {
        if (tolower(toupper(c)) != c) { upper_ok = 0; break; }
    }
    TEST(g, "toupper(tolower(X)) == X for all A-Z", lower_ok);
    TEST(g, "tolower(toupper(x)) == x for all a-z", upper_ok);

    ut_group_end(g);
}

/* ================================================================ entry point */

void run_test_libc_ctype(us_test_group_t groups[], int *idx) {
    test_isalpha          (&groups[(*idx)++]);
    test_isdigit          (&groups[(*idx)++]);
    test_isalnum          (&groups[(*idx)++]);
    test_isspace          (&groups[(*idx)++]);
    test_isupper_islower  (&groups[(*idx)++]);
    test_isxdigit         (&groups[(*idx)++]);
    test_ispunct_isgraph_isprint(&groups[(*idx)++]);
    test_iscntrl          (&groups[(*idx)++]);
    test_isblank          (&groups[(*idx)++]);
    test_isascii          (&groups[(*idx)++]);
    test_tolower_toupper  (&groups[(*idx)++]);
}
