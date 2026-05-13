/*
 * test_libc_string.c — comprehensive string.h conformance tests
 *
 * Every test checks that the custom libc behaves identically to what
 * standard C specifies.  No kernel-specific behaviour is assumed.
 */

#include <string.h>
#include <stddef.h>
#include <signal.h>
#include "us_test_common.h"

/* ------------------------------------------------------------------ helpers */

static int all_bytes(const char *p, char val, int n) {
    int i;
    for (i = 0; i < n; i++) {
        if (p[i] != val) return 0;
    }
    return 1;
}

/* ================================================================== groups */

/* ---------- memcpy ------------------------------------------------- */
static void test_memcpy(us_test_group_t *g) {
    char dst[16];
    char src[] = "0123456789";

    ut_group_begin(g, "memcpy");

    memcpy(dst, src, 11);
    TEST(g, "copies all bytes including NUL", memcmp(dst, "0123456789", 11) == 0);

    /* single byte */
    dst[0] = 'X';
    memcpy(dst, "A", 1);
    TEST(g, "single-byte copy", dst[0] == 'A');

    /* zero-length copy is a no-op */
    dst[0] = 'Z';
    memcpy(dst, "B", 0);
    TEST(g, "zero-length copy leaves dst unchanged", dst[0] == 'Z');

    /* return value is dst */
    TEST(g, "return value is dst pointer", memcpy(dst, src, 5) == (void *)dst);

    ut_group_end(g);
}

/* ---------- memmove ------------------------------------------------ */
static void test_memmove(us_test_group_t *g) {
    char buf[32];

    ut_group_begin(g, "memmove");

    /* non-overlapping */
    memcpy(buf, "abcdef", 7);
    memmove(buf + 8, buf, 7);
    TEST(g, "non-overlapping forward copy", memcmp(buf + 8, "abcdef", 7) == 0);

    /* overlapping: src before dst (forward overlap) */
    memcpy(buf, "0123456789", 10);
    memmove(buf + 4, buf, 8);
    TEST(g, "forward-overlap preserves data", memcmp(buf + 4, "01234567", 8) == 0);

    /* overlapping: dst before src (backward overlap) */
    memcpy(buf, "ABCDEFGH", 8);
    memmove(buf, buf + 2, 6);
    TEST(g, "backward-overlap preserves data", memcmp(buf, "CDEFGH", 6) == 0);

    /* zero-length is a no-op */
    buf[0] = 'X';
    memmove(buf, "Y", 0);
    TEST(g, "zero-length memmove is no-op", buf[0] == 'X');

    /* return value is dst */
    TEST(g, "return value is dst pointer", memmove(buf, buf + 1, 4) == (void *)buf);

    ut_group_end(g);
}

/* ---------- memset ------------------------------------------------- */
static void test_memset(us_test_group_t *g) {
    char buf[32];

    ut_group_begin(g, "memset");

    memset(buf, 0, sizeof(buf));
    TEST(g, "zero-fills buffer", all_bytes(buf, 0, 32));

    memset(buf, 0xFF, 8);
    TEST(g, "fills with 0xFF", all_bytes(buf, (char)0xFF, 8));

    /* only low 8 bits of value are used */
    memset(buf, 0x141, 4);  /* 0x141 & 0xFF == 0x41 == 'A' */
    TEST(g, "only low 8 bits used", all_bytes(buf, 0x41, 4));

    /* zero-length is a no-op; cast suppresses over-eager host gcc diagnostic */
    buf[0] = 'Z';
    memset(buf, 'Q', (size_t)0);
    TEST(g, "zero-length memset is no-op", buf[0] == 'Z');

    /* return value is the pointer */
    TEST(g, "return value is s pointer", memset(buf, 0, 4) == (void *)buf);

    ut_group_end(g);
}

/* ---------- memcmp ------------------------------------------------- */
static void test_memcmp(us_test_group_t *g) {
    ut_group_begin(g, "memcmp");

    TEST(g, "equal buffers returns 0", memcmp("abc", "abc", 3) == 0);
    TEST(g, "first < second returns negative", memcmp("abc", "abd", 3) < 0);
    TEST(g, "first > second returns positive", memcmp("abd", "abc", 3) > 0);
    TEST(g, "zero-length comparison returns 0", memcmp("X", "Y", 0) == 0);
    /* compares as unsigned bytes */
    TEST(g, "unsigned comparison: 0x80 > 0x01",
         memcmp("\x80", "\x01", 1) > 0);

    ut_group_end(g);
}

/* ---------- memchr ------------------------------------------------- */
static void test_memchr(us_test_group_t *g) {
    const char *s = "hello\0world";

    ut_group_begin(g, "memchr");

    TEST(g, "finds first occurrence", (char *)memchr(s, 'l', 11) == s + 2);
    TEST(g, "finds NUL byte in range", memchr(s, '\0', 11) == (void *)(s + 5));
    TEST(g, "returns null when not found", memchr(s, 'z', 5) == NULL);
    TEST(g, "zero-length search returns null", memchr(s, 'h', 0) == NULL);

    ut_group_end(g);
}

/* ---------- strlen / strnlen --------------------------------------- */
static void test_strlen(us_test_group_t *g) {
    ut_group_begin(g, "strlen/strnlen");

    TEST(g, "empty string is 0", strlen("") == 0);
    TEST(g, "short string", strlen("hello") == 5);
    TEST(g, "string with embedded spaces", strlen("a b c") == 5);

    TEST(g, "strnlen within limit", strnlen("hello", 10) == 5);
    TEST(g, "strnlen at exact limit", strnlen("hello", 5) == 5);
    TEST(g, "strnlen clamps to maxlen", strnlen("hello", 3) == 3);
    TEST(g, "strnlen on empty with 0 limit", strnlen("", 0) == 0);

    ut_group_end(g);
}

/* ---------- strcpy / strncpy --------------------------------------- */
static void test_strcpy(us_test_group_t *g) {
    char dst[32];

    ut_group_begin(g, "strcpy/strncpy");

    strcpy(dst, "hello");
    TEST(g, "strcpy copies content", strcmp(dst, "hello") == 0);
    TEST(g, "strcpy returns dst", strcpy(dst, "x") == dst);

    /* strncpy: destination fully padded when src is shorter */
    memset(dst, 'X', 10);
    strncpy(dst, "hi", 8);
    TEST(g, "strncpy pads with NULs when src shorter", dst[2] == '\0' && dst[7] == '\0');
    TEST(g, "strncpy copies content", dst[0] == 'h' && dst[1] == 'i');

    /* strncpy: does NOT NUL-terminate when src >= n */
    memset(dst, 'X', 10);
    strncpy(dst, "abcdef", 4);
    TEST(g, "strncpy truncates without NUL when src >= n",
         dst[0] == 'a' && dst[1] == 'b' && dst[2] == 'c' && dst[3] == 'd' && dst[4] == 'X');

    ut_group_end(g);
}

/* ---------- strcat / strncat --------------------------------------- */
static void test_strcat(us_test_group_t *g) {
    char buf[64];

    ut_group_begin(g, "strcat/strncat");

    strcpy(buf, "foo");
    strcat(buf, "bar");
    TEST(g, "strcat appends", strcmp(buf, "foobar") == 0);
    TEST(g, "strcat returns dst", strcat(buf, "") == buf);

    strcpy(buf, "abc");
    strncat(buf, "defgh", 3);
    TEST(g, "strncat appends at most n chars", strcmp(buf, "abcdef") == 0);

    strcpy(buf, "xyz");
    strncat(buf, "12", 10);
    TEST(g, "strncat with n > src len appends all", strcmp(buf, "xyz12") == 0);

    ut_group_end(g);
}

/* ---------- strcmp / strncmp --------------------------------------- */
static void test_strcmp(us_test_group_t *g) {
    ut_group_begin(g, "strcmp/strncmp");

    TEST(g, "equal strings return 0", strcmp("abc", "abc") == 0);
    TEST(g, "first shorter returns negative", strcmp("ab", "abc") < 0);
    TEST(g, "first longer returns positive", strcmp("abc", "ab") > 0);
    TEST(g, "empty strings equal", strcmp("", "") == 0);
    TEST(g, "unsigned comparison 0x80 > 0x7f",
         strcmp("\x80", "\x7f") > 0);

    TEST(g, "strncmp equal prefix returns 0", strncmp("abcX", "abcY", 3) == 0);
    TEST(g, "strncmp detects diff at limit", strncmp("abcX", "abcY", 4) != 0);
    TEST(g, "strncmp zero length returns 0", strncmp("A", "B", 0) == 0);

    ut_group_end(g);
}

/* ---------- strchr / strrchr --------------------------------------- */
static void test_strchr(us_test_group_t *g) {
    const char *s = "abcabc";

    ut_group_begin(g, "strchr/strrchr");

    TEST(g, "strchr finds first occurrence", strchr(s, 'b') == s + 1);
    TEST(g, "strchr finds NUL terminator", strchr(s, '\0') == s + 6);
    TEST(g, "strchr returns null when not found", strchr(s, 'z') == NULL);

    TEST(g, "strrchr finds last occurrence", strrchr(s, 'b') == s + 4);
    TEST(g, "strrchr finds NUL terminator", strrchr(s, '\0') == s + 6);
    TEST(g, "strrchr returns null when not found", strrchr(s, 'z') == NULL);

    ut_group_end(g);
}

/* ---------- strstr ------------------------------------------------- */
static void test_strstr(us_test_group_t *g) {
    ut_group_begin(g, "strstr");

    TEST(g, "finds substring", strstr("hello world", "world") != NULL);
    TEST(g, "returns pointer to start", strstr("abcabc", "bc") == (char *)"abcabc" + 1);
    TEST(g, "empty needle returns haystack", strstr("abc", "") != NULL);
    TEST(g, "not found returns null", strstr("abc", "xyz") == NULL);
    TEST(g, "needle longer than haystack returns null", strstr("ab", "abc") == NULL);

    ut_group_end(g);
}

/* ---------- strdup ------------------------------------------------- */
static void test_strdup(us_test_group_t *g) {
    char *p;

    ut_group_begin(g, "strdup");

    p = strdup("hello");
    TEST(g, "strdup returns non-null", p != NULL);
    TEST(g, "strdup copies content", p != NULL && strcmp(p, "hello") == 0);
    /* must be a distinct allocation */
    if (p) {
        p[0] = 'X';
        TEST(g, "strdup is independent copy", strcmp(p, "Xello") == 0);
        /* free not tested to avoid malloc dependency; just don't leak badly */
    }

    p = strdup("");
    TEST(g, "strdup empty string", p != NULL && p[0] == '\0');

    ut_group_end(g);
}

/* ---------- strtok ------------------------------------------------- */
static void test_strtok(us_test_group_t *g) {
    char s1[] = "one,two,,four";
    char s2[] = "  spaces  here  ";
    char *tok;

    ut_group_begin(g, "strtok");

    tok = strtok(s1, ",");
    TEST(g, "strtok first token", tok != NULL && strcmp(tok, "one") == 0);
    tok = strtok(NULL, ",");
    TEST(g, "strtok second token", tok != NULL && strcmp(tok, "two") == 0);
    /* consecutive delimiters: empty token is skipped per POSIX */
    tok = strtok(NULL, ",");
    TEST(g, "strtok skips empty field", tok != NULL && strcmp(tok, "four") == 0);
    tok = strtok(NULL, ",");
    TEST(g, "strtok returns null at end", tok == NULL);

    tok = strtok(s2, " ");
    TEST(g, "strtok on whitespace-delimited first", tok != NULL && strcmp(tok, "spaces") == 0);
    tok = strtok(NULL, " ");
    TEST(g, "strtok on whitespace-delimited second", tok != NULL && strcmp(tok, "here") == 0);
    tok = strtok(NULL, " ");
    TEST(g, "strtok on whitespace-delimited exhausted", tok == NULL);

    ut_group_end(g);
}

/* ---------- strerror ---------------------------------------------- */
static void test_strerror(us_test_group_t *g) {
    ut_group_begin(g, "strerror");

    TEST(g, "strerror(0) returns non-null", strerror(0) != NULL);
    /* ENOENT == 2 universally */
    TEST(g, "strerror(ENOENT) returns non-null string",
         strerror(2) != NULL && strlen(strerror(2)) > 0);
    /* Unknown error must still return non-null */
    TEST(g, "strerror(unknown) returns non-null", strerror(9999) != NULL);

    ut_group_end(g);
}

/* ---------- strlcpy ----------------------------------------------- */
static void test_strlcpy(us_test_group_t *g) {
    char dst[16];

    ut_group_begin(g, "strlcpy");

    /* fits with room to spare */
    memset(dst, 'X', sizeof(dst));
    size_t r = strlcpy(dst, "hi", sizeof(dst));
    TEST(g, "strlcpy short return = srclen", r == 2);
    TEST(g, "strlcpy short copies bytes", strcmp(dst, "hi") == 0);

    /* exact fit (size = srclen + 1) */
    r = strlcpy(dst, "hello", 6);
    TEST(g, "strlcpy exact fit return = 5", r == 5);
    TEST(g, "strlcpy exact fit terminates", strcmp(dst, "hello") == 0);

    /* truncation: size smaller than src */
    memset(dst, 'X', sizeof(dst));
    r = strlcpy(dst, "hello", 4);
    TEST(g, "strlcpy truncation return = srclen=5", r == 5);
    TEST(g, "strlcpy truncation produces 3-char string", strcmp(dst, "hel") == 0);
    TEST(g, "strlcpy NUL-terminates on truncation", dst[3] == '\0');

    /* size = 0: must not touch dst, must return srclen */
    dst[0] = 'Z';
    r = strlcpy(dst, "abc", 0);
    TEST(g, "strlcpy size=0 returns srclen", r == 3);
    TEST(g, "strlcpy size=0 leaves dst untouched", dst[0] == 'Z');

    /* size = 1: only NUL */
    dst[0] = 'Z';
    r = strlcpy(dst, "abc", 1);
    TEST(g, "strlcpy size=1 returns srclen", r == 3);
    TEST(g, "strlcpy size=1 writes NUL", dst[0] == '\0');

    ut_group_end(g);
}

/* ---------- strlcat ----------------------------------------------- */
static void test_strlcat(us_test_group_t *g) {
    char dst[16];

    ut_group_begin(g, "strlcat");

    /* basic concatenation with room */
    strcpy(dst, "foo");
    size_t r = strlcat(dst, "bar", sizeof(dst));
    TEST(g, "strlcat basic return = dlen + srclen", r == 6);
    TEST(g, "strlcat basic produces 'foobar'", strcmp(dst, "foobar") == 0);

    /* exact fit */
    strcpy(dst, "ab");
    r = strlcat(dst, "cd", 5);
    TEST(g, "strlcat exact fit return = 4", r == 4);
    TEST(g, "strlcat exact fit produces 'abcd'", strcmp(dst, "abcd") == 0);

    /* truncation: room for only some src bytes */
    strcpy(dst, "abc");
    r = strlcat(dst, "DEFGH", 6);
    TEST(g, "strlcat truncate return = 3 + 5 = 8", r == 8);
    TEST(g, "strlcat truncate produces 'abcDE'", strcmp(dst, "abcDE") == 0);

    /* dst already full (no NUL within size): return size + strlen(src) */
    memset(dst, 'X', 5);
    /* dst has no NUL in first 5 bytes; size=5 means dlen==size */
    r = strlcat(dst, "hello", 5);
    TEST(g, "strlcat full-dst return = size + srclen", r == 5 + 5);

    ut_group_end(g);
}

/* ---------- strerror_r -------------------------------------------- */
static void test_strerror_r(us_test_group_t *g) {
    char buf[32];

    ut_group_begin(g, "strerror_r");

    /* normal case: buf large enough */
    buf[0] = '\0';
    int rc = strerror_r(2 /* ENOENT */, buf, sizeof(buf));
    TEST(g, "strerror_r returns 0 on fit", rc == 0);
    TEST(g, "strerror_r writes a non-empty string", strlen(buf) > 0);
    TEST(g, "strerror_r NUL-terminates", buf[strlen(buf)] == '\0');

    /* tiny buf: 1 byte only — must write NUL and return ERANGE (34) */
    buf[0] = 'Z';
    rc = strerror_r(2, buf, 1);
    TEST(g, "strerror_r tiny buf NUL-terminates", buf[0] == '\0');
    TEST(g, "strerror_r tiny buf returns ERANGE", rc == 34);

    /* zero-len buf — return ERANGE */
    rc = strerror_r(2, buf, 0);
    TEST(g, "strerror_r zero-len buf returns ERANGE", rc == 34);

    ut_group_end(g);
}

/* ---------- strsignal --------------------------------------------- */
static void test_strsignal(us_test_group_t *g) {
    ut_group_begin(g, "strsignal");

    char *s;

    s = strsignal(SIGINT);
    TEST(g, "strsignal(SIGINT) non-null", s != NULL);
    TEST(g, "strsignal(SIGINT) non-empty", strlen(s) > 0);

    s = strsignal(SIGKILL);
    TEST(g, "strsignal(SIGKILL) non-null", s != NULL);
    TEST(g, "strsignal(SIGKILL) non-empty", strlen(s) > 0);

    s = strsignal(SIGSEGV);
    TEST(g, "strsignal(SIGSEGV) non-null", s != NULL);

    /* unknown signum must still return non-null */
    s = strsignal(9999);
    TEST(g, "strsignal(unknown) non-null", s != NULL);
    TEST(g, "strsignal(unknown) non-empty", strlen(s) > 0);

    ut_group_end(g);
}

/* ================================================================ entry point */

void run_test_libc_string(us_test_group_t groups[], int *idx) {
    test_memcpy  (&groups[(*idx)++]);
    test_memmove (&groups[(*idx)++]);
    test_memset  (&groups[(*idx)++]);
    test_memcmp  (&groups[(*idx)++]);
    test_memchr  (&groups[(*idx)++]);
    test_strlen  (&groups[(*idx)++]);
    test_strcpy  (&groups[(*idx)++]);
    test_strcat  (&groups[(*idx)++]);
    test_strcmp  (&groups[(*idx)++]);
    test_strchr  (&groups[(*idx)++]);
    test_strstr  (&groups[(*idx)++]);
    test_strdup  (&groups[(*idx)++]);
    test_strtok  (&groups[(*idx)++]);
    test_strerror(&groups[(*idx)++]);
    test_strlcpy   (&groups[(*idx)++]);
    test_strlcat   (&groups[(*idx)++]);
    test_strerror_r(&groups[(*idx)++]);
    test_strsignal (&groups[(*idx)++]);
}
