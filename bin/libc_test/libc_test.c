/*
 * libc_test.c — Userspace libc correctness tests
 *
 * Runs entirely in userspace via syscalls.  Each test group is independent
 * and reports [PASS]/[FAIL] to stdout so results are visible on the console.
 *
 * Tests are written only for functionality that the current kernel supports:
 *   - printf / putchar / puts / sprintf / snprintf
 *   - string.h  (no kernel dependency)
 *   - ctype.h   (no kernel dependency)
 *   - stdlib.h  (atoi, strtol, qsort, etc.)
 *   - malloc / free / calloc / realloc  (via mmap syscall)
 *   - open / read / close               (tarfs, read-only)
 *   - write to stdout (fd 1)
 *   - fork / wait / getpid
 *   - exit
 *
 * Tests that depend on unimplemented syscalls (lseek, stat, getdents64, etc.)
 * are clearly marked SKIP so it's easy to enable them later.
 */

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"
#include "fcntl.h"
#include "unistd.h"
#include "sys/wait.h"
#include "sys/types.h"

/* ------------------------------------------------------------------ */
/* Test harness                                                         */
/* ------------------------------------------------------------------ */

static int g_passed = 0;
static int g_failed = 0;
static int g_skipped = 0;

#define TEST(name, cond) do {                                        \
    if (cond) {                                                       \
        printf("  [PASS] %s\n", name);                               \
        g_passed++;                                                   \
    } else {                                                          \
        printf("  [FAIL] %s  (%s:%d)\n", name, __FILE__, __LINE__);  \
        g_failed++;                                                   \
    }                                                                 \
} while (0)

#define SKIP(name) do {                                              \
    printf("  [SKIP] %s  (kernel syscall not implemented)\n", name); \
    g_skipped++;                                                     \
} while (0)

/* ------------------------------------------------------------------ */
/* 1. printf / formatting                                              */
/* ------------------------------------------------------------------ */

static void test_printf(void) {
    printf("\n=== printf / formatting ===\n");

    /* Basic string printing — if this works, the whole I/O path is up */
    printf("  [INFO] stdout write path: printf -> ecall SYS_write(4) -> printk\n");

    /* snprintf truncation */
    char small[8];
    int ret = snprintf(small, sizeof(small), "abcdefghij");
    TEST("snprintf truncates and NUL-terminates", small[7] == '\0' && ret == 10);
    TEST("snprintf content is truncated prefix",  strcmp(small, "abcdefg") == 0);

    /* sprintf into a local buffer */
    char buf[64];
    int n = sprintf(buf, "hello %s %d", "world", 42);
    TEST("sprintf basic", n == 14 && strcmp(buf, "hello world 42") == 0);

    /* %d negatives */
    sprintf(buf, "%d", -999);
    TEST("sprintf %%d negative", strcmp(buf, "-999") == 0);

    /* %u */
    sprintf(buf, "%u", 4294967295U);
    TEST("sprintf %%u max uint", strcmp(buf, "4294967295") == 0);

    /* %x */
    sprintf(buf, "%x", 0xdeadbeef);
    TEST("sprintf %%x hex", strcmp(buf, "deadbeef") == 0);

    /* %p prefix */
    sprintf(buf, "%p", (void *)0xabcd);
    TEST("sprintf %%p has 0x prefix", buf[0] == '0' && buf[1] == 'x');

    /* %ld */
    sprintf(buf, "%ld", -1234567890L);
    TEST("sprintf %%ld long", strcmp(buf, "-1234567890") == 0);

    /* %lu */
    sprintf(buf, "%lu", 9876543210UL);
    TEST("sprintf %%lu unsigned long", strcmp(buf, "9876543210") == 0);

    /* %c */
    sprintf(buf, "%c", 'Z');
    TEST("sprintf %%c char", buf[0] == 'Z' && buf[1] == '\0');

    /* %% literal */
    sprintf(buf, "100%%");
    TEST("sprintf %%%% literal", strcmp(buf, "100%") == 0);

    /* NULL string pointer */
    sprintf(buf, "%s", (char *)0);
    TEST("sprintf %%s NULL prints (null)", strcmp(buf, "(null)") == 0);
}

/* ------------------------------------------------------------------ */
/* 2. string.h                                                          */
/* ------------------------------------------------------------------ */

static void test_string(void) {
    printf("\n=== string.h ===\n");

    /* strlen */
    TEST("strlen empty",  strlen("") == 0);
    TEST("strlen hello",  strlen("hello") == 5);

    /* strcmp */
    TEST("strcmp equal",   strcmp("abc", "abc") == 0);
    TEST("strcmp less",    strcmp("abc", "abd") < 0);
    TEST("strcmp greater", strcmp("abd", "abc") > 0);

    /* strncmp */
    TEST("strncmp prefix", strncmp("abcXXX", "abcYYY", 3) == 0);
    TEST("strncmp differ", strncmp("abcX", "abcY", 4) != 0);

    /* strcpy / strncpy */
    char dst[16];
    strcpy(dst, "hello");
    TEST("strcpy", strcmp(dst, "hello") == 0);

    strncpy(dst, "world!", 5);
    dst[5] = '\0';
    TEST("strncpy 5 chars", strcmp(dst, "world") == 0);

    /* strcat / strncat */
    strcpy(dst, "foo");
    strcat(dst, "bar");
    TEST("strcat", strcmp(dst, "foobar") == 0);

    strcpy(dst, "foo");
    strncat(dst, "barXXX", 3);
    TEST("strncat 3", strcmp(dst, "foobar") == 0);

    /* strchr / strrchr */
    const char *s = "abcabc";
    TEST("strchr first",  strchr(s,  'b') == s + 1);
    TEST("strrchr last",  strrchr(s, 'b') == s + 4);
    TEST("strchr absent", strchr(s, 'z') == NULL);

    /* strstr */
    TEST("strstr found",  strstr("hello world", "world") != NULL);
    TEST("strstr absent", strstr("hello world", "xyz")   == NULL);
    TEST("strstr empty needle", strstr("hello", "") != NULL);

    /* memcpy / memset / memcmp / memmove */
    char a[8], b[8];
    memset(a, 0xAA, 8);
    TEST("memset all 0xAA", a[0] == (char)0xAA && a[7] == (char)0xAA);

    memcpy(b, a, 8);
    TEST("memcpy exact copy", memcmp(a, b, 8) == 0);

    b[3] = 0;
    TEST("memcmp detects diff", memcmp(a, b, 8) != 0);

    /* memmove overlapping */
    char ov[8] = "abcdefg";
    memmove(ov + 1, ov, 4);   /* shift right one */
    TEST("memmove overlap src < dst", ov[1] == 'a' && ov[2] == 'b');

    /* strdup */
    char *dup = strdup("test_strdup");
    TEST("strdup not NULL",  dup != NULL);
    TEST("strdup content",   dup != NULL && strcmp(dup, "test_strdup") == 0);
    free(dup);

    /* strtok */
    char tok_src[] = "one,two,,three";
    char *tok = strtok(tok_src, ",");
    TEST("strtok first token",  tok != NULL && strcmp(tok, "one") == 0);
    tok = strtok(NULL, ",");
    TEST("strtok second token", tok != NULL && strcmp(tok, "two") == 0);
    tok = strtok(NULL, ",");
    TEST("strtok skips empty",  tok != NULL && strcmp(tok, "three") == 0);
    tok = strtok(NULL, ",");
    TEST("strtok returns NULL at end", tok == NULL);
}

/* ------------------------------------------------------------------ */
/* 3. ctype.h                                                           */
/* ------------------------------------------------------------------ */

static void test_ctype(void) {
    printf("\n=== ctype.h ===\n");

    TEST("isalpha 'A'",  isalpha('A'));
    TEST("isalpha '1'", !isalpha('1'));
    TEST("isdigit '5'",  isdigit('5'));
    TEST("isdigit 'a'", !isdigit('a'));
    TEST("isspace ' '",  isspace(' '));
    TEST("isspace '\\n'", isspace('\n'));
    TEST("isspace 'x'", !isspace('x'));
    TEST("isupper 'Z'",  isupper('Z'));
    TEST("islower 'z'",  islower('z'));
    TEST("isalnum '0'",  isalnum('0'));
    TEST("isalnum '!'", !isalnum('!'));
    TEST("tolower 'A' -> 'a'", tolower('A') == 'a');
    TEST("toupper 'z' -> 'Z'", toupper('z') == 'Z');
    TEST("tolower '1' unchanged", tolower('1') == '1');
    TEST("isprint ' '",  isprint(' '));
    TEST("isprint '~'",  isprint('~'));
    TEST("isprint '\\0'", !isprint('\0'));
}

/* ------------------------------------------------------------------ */
/* 4. stdlib.h numeric conversions                                      */
/* ------------------------------------------------------------------ */

static void test_stdlib_numeric(void) {
    printf("\n=== stdlib.h: numeric conversions ===\n");

    TEST("atoi \"0\"",      atoi("0") == 0);
    TEST("atoi \"42\"",     atoi("42") == 42);
    TEST("atoi \"-7\"",     atoi("-7") == -7);
    TEST("atoi \"  9\"",    atoi("  9") == 9);   /* leading spaces */
    TEST("atoi \"99abc\"",  atoi("99abc") == 99); /* stops at non-digit */

    TEST("strtol base10",    strtol("12345",  NULL, 10) == 12345L);
    TEST("strtol base16 0x", strtol("0xff",   NULL, 16) == 255L);
    TEST("strtol base2",     strtol("101",    NULL, 2)  == 5L);
    TEST("strtol base0 hex", strtol("0x1A",   NULL, 0)  == 26L);
    TEST("strtol base0 oct", strtol("017",    NULL, 0)  == 15L);
    TEST("strtol negative",  strtol("-100",   NULL, 10) == -100L);

    char *end;
    long v = strtol("  42rest", &end, 10);
    TEST("strtol endptr",    v == 42 && strcmp(end, "rest") == 0);

    TEST("strtoul max-ish", strtoul("4294967295", NULL, 10) == 4294967295UL);

    TEST("abs positive", abs(7)   == 7);
    TEST("abs negative", abs(-7)  == 7);
    TEST("abs zero",     abs(0)   == 0);
    TEST("labs",         labs(-1000000L) == 1000000L);
}

/* ------------------------------------------------------------------ */
/* 5. stdlib.h: qsort / bsearch                                        */
/* ------------------------------------------------------------------ */

static int int_cmp(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

static void test_stdlib_sort(void) {
    printf("\n=== stdlib.h: qsort / bsearch ===\n");

    int arr[] = { 5, 3, 8, 1, 9, 2, 7, 4, 6, 0 };
    int sz = 10;
    qsort(arr, sz, sizeof(int), int_cmp);

    int sorted = 1;
    for (int i = 0; i < sz - 1; i++)
        if (arr[i] > arr[i+1]) { sorted = 0; break; }
    TEST("qsort 10 ints ascending", sorted);
    TEST("qsort smallest is 0", arr[0] == 0);
    TEST("qsort largest is 9",  arr[sz-1] == 9);

    int key = 7;
    int *found = (int *)bsearch(&key, arr, sz, sizeof(int), int_cmp);
    TEST("bsearch finds 7",     found != NULL && *found == 7);

    key = 99;
    int *notfound = (int *)bsearch(&key, arr, sz, sizeof(int), int_cmp);
    TEST("bsearch returns NULL for absent key", notfound == NULL);

    /* Already-sorted array */
    int sorted_arr[] = { 1, 2, 3 };
    qsort(sorted_arr, 3, sizeof(int), int_cmp);
    TEST("qsort already sorted", sorted_arr[0] == 1 && sorted_arr[2] == 3);

    /* Single element */
    int one = 42;
    qsort(&one, 1, sizeof(int), int_cmp);
    TEST("qsort single element", one == 42);
}

/* ------------------------------------------------------------------ */
/* 6. malloc / free / calloc / realloc                                 */
/* ------------------------------------------------------------------ */

static void test_malloc(void) {
    printf("\n=== malloc / free / calloc / realloc ===\n");

    /* Basic alloc and free */
    void *p = malloc(64);
    TEST("malloc(64) non-NULL", p != NULL);
    free(p);

    /* Write and read back */
    char *buf = (char *)malloc(128);
    TEST("malloc(128) non-NULL", buf != NULL);
    for (int i = 0; i < 128; i++) buf[i] = (char)i;
    int ok = 1;
    for (int i = 0; i < 128; i++)
        if (buf[i] != (char)i) { ok = 0; break; }
    TEST("malloc write/read back 128 bytes", ok);
    free(buf);

    /* calloc zeroed */
    int *arr = (int *)calloc(16, sizeof(int));
    TEST("calloc non-NULL", arr != NULL);
    int zeroed = 1;
    for (int i = 0; i < 16; i++)
        if (arr[i] != 0) { zeroed = 0; break; }
    TEST("calloc memory is zeroed", zeroed);
    free(arr);

    /* realloc grows */
    char *r = (char *)malloc(32);
    TEST("malloc(32) for realloc", r != NULL);
    for (int i = 0; i < 32; i++) r[i] = (char)i;
    r = (char *)realloc(r, 128);
    TEST("realloc(128) non-NULL", r != NULL);
    int realloc_ok = 1;
    for (int i = 0; i < 32; i++)
        if (r[i] != (char)i) { realloc_ok = 0; break; }
    TEST("realloc preserves old data", realloc_ok);
    free(r);

    /* Many small allocations */
#define N 64
    void *ptrs[N];
    int all_ok = 1;
    for (int i = 0; i < N; i++) {
        ptrs[i] = malloc(16 + i * 4);
        if (!ptrs[i]) { all_ok = 0; break; }
    }
    TEST("64 small mallocs succeed", all_ok);
    for (int i = 0; i < N; i++) if (ptrs[i]) free(ptrs[i]);
#undef N

    /* Large allocation (>= 4KB, goes through direct mmap path) */
    void *big = malloc(8192);
    TEST("malloc(8192) large alloc non-NULL", big != NULL);
    if (big) {
        /* touch every page */
        char *cb = (char *)big;
        for (int i = 0; i < 8192; i += 4096) cb[i] = (char)i;
        TEST("large alloc pages writable", cb[0] == 0 && cb[4096] == (char)4096);
        free(big);
    }

    /* malloc(0) — implementation defined, just shouldn't crash */
    void *z = malloc(0);
    free(z);   /* safe to call free on NULL or a zero-size block */
    TEST("malloc(0) does not crash", 1);
}

/* ------------------------------------------------------------------ */
/* 7. open / read (tarfs)                                              */
/* ------------------------------------------------------------------ */

static void test_file_io(void) {
    printf("\n=== open / read (tarfs) ===\n");

    /* hello.txt is always in rootfs/hello.txt */
    int fd = open("/hello.txt", O_RDONLY);
    TEST("open /hello.txt returns fd >= 3", fd >= 3);

    if (fd >= 0) {
        char buf[64];
        int n = read(fd, buf, sizeof(buf) - 1);
        TEST("read returns > 0 bytes", n > 0);
        if (n > 0) {
            buf[n] = '\0';
            TEST("read content is non-empty string", buf[0] != '\0');
            printf("  [INFO] /hello.txt content: %s\n", buf);
        }

        /* Second read on same fd continues from offset */
        char buf2[16];
        int n2 = read(fd, buf2, sizeof(buf2));
        /* Either more data or EOF (0), but not an error (-1) */
        TEST("second read does not error", n2 >= 0);

        int r = close(fd);
        TEST("close returns 0", r == 0);
    }

    /* Nonexistent file */
    int bad = open("/no_such_file_xyz.txt", O_RDONLY);
    TEST("open nonexistent file returns < 0", bad < 0);

    /* Multiple files can be opened independently */
    int fd1 = open("/hello.txt", O_RDONLY);
    int fd2 = open("/hello.txt", O_RDONLY);
    TEST("two concurrent opens get distinct fds", fd1 >= 0 && fd2 >= 0 && fd1 != fd2);
    if (fd1 >= 0) close(fd1);
    if (fd2 >= 0) close(fd2);

    /* write to file fd (should fail — sys_write only handles fd 1/2) */
    int wfd = open("/hello.txt", O_RDONLY);
    if (wfd >= 0) {
        char wb[] = "x";
        int wn = write(wfd, wb, 1);
        TEST("write to file fd fails (expected -1)", wn < 0);
        close(wfd);
    }

    /* lseek — SKIP: syscall number collides with SYS_exec */
    SKIP("lseek (a7=8 collides with SYS_exec — dangerous)");

    /* stat — SKIP: no kernel handler */
    SKIP("stat (SYS_stat not in kernel switch)");

    /* getdents64 / readdir — SKIP: no kernel handler */
    SKIP("readdir / getdents64 (SYS_getdents64 not in kernel switch)");
}

/* ------------------------------------------------------------------ */
/* 8. fork / wait / getpid                                             */
/* ------------------------------------------------------------------ */

static void test_process(void) {
    printf("\n=== fork / wait / getpid ===\n");

    pid_t my_pid = getpid();
    TEST("getpid returns > 0", my_pid > 0);

    pid_t child = fork();
    if (child == 0) {
        /* Child: verify its own pid differs from parent, then exit */
        pid_t cpid = getpid();
        if (cpid <= 0 || cpid == my_pid) {
            /* Signal failure via exit code */
            exit(1);
        }
        exit(0);
    }

    TEST("fork returns child pid > 0 in parent", child > 0);

    int status = -1;
    pid_t waited = wait(&status);
    TEST("wait returns child pid", waited == child);
    TEST("child exited with status 0", status == 0);

    /* fork + verify child memory is independent */
    int shared = 42;
    pid_t child2 = fork();
    if (child2 == 0) {
        shared = 999;   /* modify in child — must not affect parent */
        exit(0);
    }
    wait(NULL);
    TEST("child write does not affect parent memory", shared == 42);
}

/* ------------------------------------------------------------------ */
/* 9. write to stdout / stderr                                          */
/* ------------------------------------------------------------------ */

static void test_write_stdout(void) {
    printf("\n=== write to stdout / stderr ===\n");

    const char *msg = "test stdout write\n";
    int n = write(STDOUT_FILENO, msg, strlen(msg));
    TEST("write(1,...) returns byte count", n == (int)strlen(msg));

    const char *errmsg = "test stderr write\n";
    int m = write(STDERR_FILENO, errmsg, strlen(errmsg));
    TEST("write(2,...) returns byte count", m == (int)strlen(errmsg));
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void) {
    printf("\n");
    printf("========================================\n");
    printf("       libc Userspace Test Suite\n");
    printf("========================================\n");
    printf("\n");

    test_printf();
    test_string();
    test_ctype();
    test_stdlib_numeric();
    test_stdlib_sort();
    test_malloc();
    test_file_io();
    test_process();
    test_write_stdout();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed, %d skipped\n",
           g_passed, g_failed, g_skipped);
    printf("========================================\n");

    if (g_failed > 0) {
        printf("WARN: %d test(s) FAILED\n", g_failed);
        return 1;
    }
    return 0;
}
