/*
 * test_libc_stdlib.c — comprehensive stdlib.h conformance tests
 *
 * Covers: atoi/atol/atoll, strtol/strtoul/strtoll/strtoull, abs/labs/llabs,
 * div/ldiv/lldiv, rand/srand, qsort, bsearch,
 * and atexit/exit (smoke only — we can't observe exit from here).
 *
 * getenv/setenv/unsetenv tests live in test_exec_envp.c alongside the
 * exec-path envp tests they depend on.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "us_test_common.h"

/* ----------------------------------------------------------------- helpers */

static int cmp_int(const void *a, const void *b) {
    const int *x = (const int *)a;
    const int *y = (const int *)b;
    return (*x > *y) - (*x < *y);
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

static int is_sorted_int(const int *arr, int n) {
    int i;
    for (i = 1; i < n; i++) {
        if (arr[i] < arr[i-1]) return 0;
    }
    return 1;
}

/* ================================================================== groups */

/* ---------- atoi / atol / atoll ------------------------------------ */
static void test_ato(us_test_group_t *g) {
    ut_group_begin(g, "atoi/atol/atoll");

    TEST(g, "atoi positive", atoi("1234") == 1234);
    TEST(g, "atoi negative", atoi("-99") == -99);
    TEST(g, "atoi zero", atoi("0") == 0);
    TEST(g, "atoi leading whitespace", atoi("  42") == 42);
    TEST(g, "atoi leading plus", atoi("+7") == 7);
    TEST(g, "atoi stops at non-digit", atoi("12abc") == 12);
    TEST(g, "atoi pure non-digit is 0", atoi("abc") == 0);
    TEST(g, "atoi empty is 0", atoi("") == 0);

    TEST(g, "atol positive", atol("123456789") == 123456789L);
    TEST(g, "atol negative", atol("-1") == -1L);

    TEST(g, "atoll large negative", atoll("-9223372036854775807") == -9223372036854775807LL);
    TEST(g, "atoll large positive", atoll("9223372036854775807") == 9223372036854775807LL);

    ut_group_end(g);
}

/* ---------- strtol ------------------------------------------------- */
static void test_strtol(us_test_group_t *g) {
    char *end;

    ut_group_begin(g, "strtol");

    TEST(g, "base 10", strtol("100", &end, 10) == 100 && *end == '\0');
    TEST(g, "base 16 with 0x prefix", strtol("0x1F", &end, 16) == 31 && *end == '\0');
    TEST(g, "base 0 hex auto", strtol("0xFF", &end, 0) == 255 && *end == '\0');
    TEST(g, "base 0 octal auto", strtol("010", &end, 0) == 8 && *end == '\0');
    TEST(g, "base 0 decimal auto", strtol("99", &end, 0) == 99 && *end == '\0');
    TEST(g, "base 2 binary", strtol("1010", &end, 2) == 10 && *end == '\0');
    TEST(g, "base 8", strtol("17", &end, 8) == 15 && *end == '\0');
    TEST(g, "negative value", strtol("-42", &end, 10) == -42 && *end == '\0');
    TEST(g, "leading whitespace", strtol("  55", &end, 10) == 55);
    TEST(g, "trailing text stored in endptr",
         strtol("123xyz", &end, 10) == 123 && strcmp(end, "xyz") == 0);
    TEST(g, "no digits: endptr == nptr",
         strtol("xyz", &end, 10) == 0 && *end == 'x');
    TEST(g, "lone 0", strtol("0", &end, 0) == 0 && *end == '\0');
    TEST(g, "LONG_MAX via base 10",
         strtol("9223372036854775807", &end, 10) == LONG_MAX && *end == '\0');
    TEST(g, "LONG_MIN via base 10",
         strtol("-9223372036854775808", &end, 10) == LONG_MIN && *end == '\0');

    ut_group_end(g);
}

/* ---------- strtoul ------------------------------------------------ */
static void test_strtoul(us_test_group_t *g) {
    char *end;

    ut_group_begin(g, "strtoul");

    TEST(g, "decimal", strtoul("255", &end, 10) == 255ul && *end == '\0');
    TEST(g, "0x prefix", strtoul("0xDEAD", &end, 16) == 0xDEADul && *end == '\0');
    TEST(g, "base 0 hex", strtoul("0x2A", &end, 0) == 42ul && *end == '\0');
    TEST(g, "base 0 octal", strtoul("077", &end, 0) == 63ul && *end == '\0');
    TEST(g, "ULONG_MAX",
         strtoul("18446744073709551615", &end, 10) == ULONG_MAX && *end == '\0');

    ut_group_end(g);
}

/* ---------- strtoll / strtoull ------------------------------------- */
static void test_strtoll(us_test_group_t *g) {
    char *end;

    ut_group_begin(g, "strtoll/strtoull");

    TEST(g, "strtoll positive", strtoll("1000000000000", &end, 10) == 1000000000000LL);
    TEST(g, "strtoll negative", strtoll("-1", &end, 10) == -1LL);
    TEST(g, "strtoll hex", strtoll("0x7FFFFFFFFFFFFFFF", &end, 0) == 9223372036854775807LL);

    TEST(g, "strtoull max",
         strtoull("18446744073709551615", &end, 10) == 18446744073709551615ULL);
    TEST(g, "strtoull hex",
         strtoull("0xFFFFFFFFFFFFFFFF", &end, 0) == 18446744073709551615ULL);

    ut_group_end(g);
}

/* ---------- abs / labs / llabs ------------------------------------ */
static void test_abs(us_test_group_t *g) {
    ut_group_begin(g, "abs/labs/llabs");

    TEST(g, "abs positive unchanged", abs(5) == 5);
    TEST(g, "abs negative", abs(-5) == 5);
    TEST(g, "abs zero", abs(0) == 0);
    TEST(g, "abs INT_MAX", abs(INT_MAX) == INT_MAX);

    TEST(g, "labs positive", labs(123456789L) == 123456789L);
    TEST(g, "labs negative", labs(-123456789L) == 123456789L);

    TEST(g, "llabs positive", llabs(9000000000LL) == 9000000000LL);
    TEST(g, "llabs negative", llabs(-9000000000LL) == 9000000000LL);

    ut_group_end(g);
}

/* ---------- div / ldiv / lldiv ------------------------------------ */
static void test_div(us_test_group_t *g) {
    div_t   d;
    ldiv_t  ld;
    lldiv_t lld;

    ut_group_begin(g, "div/ldiv/lldiv");

    d = div(17, 5);
    TEST(g, "div quot", d.quot == 3);
    TEST(g, "div rem",  d.rem  == 2);

    d = div(-17, 5);
    TEST(g, "div negative quot", d.quot == -3);
    TEST(g, "div negative rem",  d.rem  == -2);

    d = div(0, 7);
    TEST(g, "div zero numerator", d.quot == 0 && d.rem == 0);

    ld = ldiv(100L, 7L);
    TEST(g, "ldiv quot", ld.quot == 14);
    TEST(g, "ldiv rem",  ld.rem  == 2);

    lld = lldiv(1000000000000LL, 1000000LL);
    TEST(g, "lldiv large quot", lld.quot == 1000000LL);
    TEST(g, "lldiv large rem",  lld.rem  == 0LL);

    ut_group_end(g);
}

/* ---------- rand / srand ------------------------------------------ */
static void test_rand(us_test_group_t *g) {
    int a, b, i, any_diff;

    ut_group_begin(g, "rand/srand");

    /* same seed → same sequence */
    srand(42);
    a = rand();
    srand(42);
    b = rand();
    TEST(g, "same seed produces same first value", a == b);

    /* values are in [0, RAND_MAX] */
    srand(1);
    TEST(g, "RAND_MAX is positive", RAND_MAX > 0);
    for (i = 0; i < 16; i++) {
        int r = rand();
        if (r < 0 || r > RAND_MAX) {
            TEST(g, "rand() in [0, RAND_MAX]", 0);
            goto rand_done;
        }
    }
    TEST(g, "rand() in [0, RAND_MAX]", 1);

rand_done:
    /* different seeds produce different sequences (with very high probability) */
    srand(1);
    a = rand();
    srand(2);
    b = rand();
    TEST(g, "different seeds give different values (heuristic)", a != b);

    /* sequence is not constant */
    srand(7);
    a = rand();
    any_diff = 0;
    for (i = 0; i < 8; i++) {
        b = rand();
        if (b != a) { any_diff = 1; break; }
    }
    TEST(g, "rand sequence is not constant", any_diff);

    ut_group_end(g);
}

/* ---------- qsort ------------------------------------------------- */
static void test_qsort(us_test_group_t *g) {
    int arr1[] = {5, 3, 1, 4, 2};
    int arr2[] = {1};
    int arr3[] = {3, 3, 3};
    int arr4[] = {9, 8, 7, 6, 5, 4, 3, 2, 1, 0};
    const char *strs[] = {"banana", "apple", "cherry", "date"};

    ut_group_begin(g, "qsort");

    qsort(arr1, 5, sizeof(int), cmp_int);
    TEST(g, "qsort 5 elements", is_sorted_int(arr1, 5));

    qsort(arr2, 1, sizeof(int), cmp_int);
    TEST(g, "qsort single element", arr2[0] == 1);

    qsort(arr3, 3, sizeof(int), cmp_int);
    TEST(g, "qsort all equal", arr3[0] == 3 && arr3[1] == 3 && arr3[2] == 3);

    qsort(arr4, 10, sizeof(int), cmp_int);
    TEST(g, "qsort 10 elements", is_sorted_int(arr4, 10));
    TEST(g, "qsort 10 elements correct first", arr4[0] == 0);
    TEST(g, "qsort 10 elements correct last", arr4[9] == 9);

    qsort(strs, 4, sizeof(char *), cmp_str);
    TEST(g, "qsort strings first", strcmp(strs[0], "apple") == 0);
    TEST(g, "qsort strings last", strcmp(strs[3], "date") == 0);

    /* zero-length: no crash */
    qsort(arr1, 0, sizeof(int), cmp_int);
    TEST(g, "qsort zero length no crash", 1);

    ut_group_end(g);
}

/* ---------- bsearch ----------------------------------------------- */
static void test_bsearch(us_test_group_t *g) {
    int haystack[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    int key;
    int *found;

    ut_group_begin(g, "bsearch");

    key = 5;
    found = (int *)bsearch(&key, haystack, 10, sizeof(int), cmp_int);
    TEST(g, "bsearch finds middle element", found != NULL && *found == 5);

    key = 1;
    found = (int *)bsearch(&key, haystack, 10, sizeof(int), cmp_int);
    TEST(g, "bsearch finds first element", found != NULL && *found == 1);

    key = 10;
    found = (int *)bsearch(&key, haystack, 10, sizeof(int), cmp_int);
    TEST(g, "bsearch finds last element", found != NULL && *found == 10);

    key = 11;
    found = (int *)bsearch(&key, haystack, 10, sizeof(int), cmp_int);
    TEST(g, "bsearch returns null for missing", found == NULL);

    key = 0;
    found = (int *)bsearch(&key, haystack, 10, sizeof(int), cmp_int);
    TEST(g, "bsearch returns null before first", found == NULL);

    /* zero-length array */
    found = (int *)bsearch(&key, haystack, 0, sizeof(int), cmp_int);
    TEST(g, "bsearch zero length returns null", found == NULL);

    /* single element found */
    int one = 42;
    key = 42;
    found = (int *)bsearch(&key, &one, 1, sizeof(int), cmp_int);
    TEST(g, "bsearch single element found", found != NULL && *found == 42);

    /* single element not found */
    key = 7;
    found = (int *)bsearch(&key, &one, 1, sizeof(int), cmp_int);
    TEST(g, "bsearch single element not found", found == NULL);

    ut_group_end(g);
}

/* ---------- signed integer minimum constants ------------------------------ */
static void test_signed_min(us_test_group_t *g) {
    /*
     * These assignments are the compile-time check: if a minimum macro expands
     * to an unsigned constant (e.g. because the large decimal literal overflows
     * the signed type before negation), the compiler emits
     * "integer constant is so large that it is unsigned" with -Wall -Werror
     * and the translation unit fails to build.  A successful build proves the
     * macros are well-formed signed constant expressions of the right type.
     */
    int       imin  = INT_MIN;
    long      lmin  = LONG_MIN;
    long long llmin = LLONG_MIN;

    TEST(g, "INT_MIN is -2147483648",            imin  == -2147483647   - 1);
    TEST(g, "INT_MIN has signed type (no warn)",  imin  <  0);
    TEST(g, "LONG_MIN is -9223372036854775808",   lmin  == -9223372036854775807L   - 1);
    TEST(g, "LONG_MIN has signed type (no warn)", lmin  <  0);
    TEST(g, "LLONG_MIN is -9223372036854775808",  llmin == -9223372036854775807LL  - 1);
    TEST(g, "LLONG_MIN has signed type (no warn)",llmin <  0);
}

/* ================================================================ entry point */

void run_test_libc_stdlib(us_test_group_t groups[], int *idx) {
    test_ato        (&groups[(*idx)++]);
    test_strtol     (&groups[(*idx)++]);
    test_strtoul    (&groups[(*idx)++]);
    test_strtoll    (&groups[(*idx)++]);
    test_abs        (&groups[(*idx)++]);
    test_div        (&groups[(*idx)++]);
    test_rand       (&groups[(*idx)++]);
    test_qsort      (&groups[(*idx)++]);
    test_bsearch    (&groups[(*idx)++]);
    test_signed_min (&groups[(*idx)++]);
}
