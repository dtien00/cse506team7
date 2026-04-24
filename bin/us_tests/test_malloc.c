#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "us_test_common.h"

static int bytes_match(const char *buf, char value, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (buf[i] != value) {
            return 0;
        }
    }
    return 1;
}

static int bytes_are_zero(const char *buf, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (buf[i] != 0) {
            return 0;
        }
    }
    return 1;
}

void run_test_malloc(us_test_group_t *group) {
    char *p;
    char *q;
    char *r;
    char *a;
    char *b;
    size_t i;
    int ok;

    ut_group_begin(group, "malloc.c");

    TEST(group, "malloc(0) returns null", malloc(0) == 0);

    p = (char *)malloc(64);
    TEST(group, "malloc returns non-null", p != 0);
    if (p != 0) {
        memset(p, 'A', 64);
        TEST(group, "malloc region writable", bytes_match(p, 'A', 64));
        free(p);
    }

    p = (char *)calloc(16, 4);
    TEST(group, "calloc returns non-null", p != 0);
    if (p != 0) {
        TEST(group, "calloc zero-initializes", bytes_are_zero(p, 64));
    }

    q = (char *)realloc(p, 64);
    TEST(group, "realloc grows allocation", q != 0);
    if (q != 0) {
        TEST(group, "realloc preserves prefix", bytes_are_zero(q, 64));
        free(q);
    } else if (p != 0) {
        free(p);
    }

    p = (char *)malloc(32);
    TEST(group, "second malloc returns non-null", p != 0);
    if (p != 0) {
        memset(p, 'B', 32);
        q = (char *)realloc(p, 96);
        TEST(group, "realloc preserves full initialized prefix",
             q != 0 && bytes_match(q, 'B', 32));
        if (q != 0) {
            free(q);
        } else {
            free(p);
        }
    }

    p = (char *)malloc(48);
    TEST(group, "malloc for shrink test returns non-null", p != 0);
    if (p != 0) {
        memset(p, 'C', 48);
        q = (char *)realloc(p, 24);
        TEST(group, "realloc shrink keeps same allocation", q == p);
        TEST(group, "realloc shrink preserves contents",
             q != 0 && bytes_match(q, 'C', 24));
        free(q);
    }

    q = (char *)realloc(0, 40);
    TEST(group, "realloc null behaves like malloc", q != 0);
    if (q != 0) {
        memset(q, 'D', 40);
        TEST(group, "realloc(null, n) buffer writable", bytes_match(q, 'D', 40));
        free(q);
    }

    p = (char *)malloc(24);
    TEST(group, "malloc for realloc-to-zero returns non-null", p != 0);
    if (p != 0) {
        memset(p, 'E', 24);
        TEST(group, "realloc(ptr, 0) returns null", realloc(p, 0) == 0);
    }

    free(0);
    TEST(group, "free(null) is a no-op", 1);

    p = (char *)malloc(64);
    TEST(group, "malloc for reuse test returns non-null", p != 0);
    if (p != 0) {
        memset(p, 'R', 64);
        free(p);
        q = (char *)malloc(32);
        TEST(group, "freed block can satisfy a later request", q != 0);
        if (q != 0) {
            memset(q, 'r', 32);
            TEST(group, "reused block remains writable", bytes_match(q, 'r', 32));
            free(q);
        }
    }

    a = (char *)malloc(64);
    b = (char *)malloc(64);
    TEST(group, "adjacent allocations succeed", a != 0 && b != 0);
    if (a != 0 && b != 0) {
        memset(a, 'X', 64);
        memset(b, 'Y', 64);
        free(b);
        free(a);
        q = (char *)malloc(96);
        TEST(group, "coalesced free blocks satisfy larger request", q != 0);
        if (q != 0) {
            memset(q, 'Z', 96);
            TEST(group, "coalesced block remains writable", bytes_match(q, 'Z', 96));
            free(q);
        }
    } else {
        free(a);
        free(b);
    }

    p = (char *)malloc(5000);
    TEST(group, "large malloc returns non-null", p != 0);
    if (p != 0) {
        memset(p, 'L', 5000);
        TEST(group, "large malloc region writable", p[0] == 'L' && p[4999] == 'L');
        free(p);
    }

    p = (char *)calloc((size_t)-1, 2);
    TEST(group, "calloc overflow returns null", p == 0);

    p = (char *)malloc(8);
    q = (char *)malloc(8);
    r = (char *)malloc(8);
    TEST(group, "small allocation burst succeeds", p != 0 && q != 0 && r != 0);
    if (p != 0 && q != 0 && r != 0) {
        for (i = 0; i < 8; i++) {
            p[i] = (char)('0' + i);
            q[i] = (char)('a' + i);
            r[i] = (char)('A' + i);
        }
        ok = memcmp(p, "01234567", 8) == 0 &&
             memcmp(q, "abcdefgh", 8) == 0 &&
             memcmp(r, "ABCDEFGH", 8) == 0;
        TEST(group, "neighboring allocations keep independent contents", ok);
    }
    free(p);
    free(q);
    free(r);

    /* ── Lazy allocation / demand-paging stress test ────────────────────────
     *
     * Allocate a single page, then fork NCHILDREN children.  Every child writes
     * to buf[0] (the same page) and exits.
     *
     * With EAGER allocation sys_brk immediately backs the page with a physical
     * frame.  Each child's write triggers a COW fault that needs a fresh frame;
     * with limited RAM, later children run out.
     *
     * With LAZY allocation the parent holds no physical frame for the page.
     * Each child demand-faults its own private copy on first write, costing
     * exactly 1 frame per child — no upfront cost in the parent at all.
     *
     * Success criterion: all children exit 0.
     */
#define LAZY_NCHILDREN 16
    {
        char *buf = (char *)malloc(4096*4096*50);
        // char *buf2 = (char *)(4096*4096*50);
        // (void) buf2;
        buf[0] = 'b'; // for fun
        TEST(group, "lazy-alloc: malloc one page returns non-null", buf != 0);
        if (buf != 0) {
            pid_t pids[LAZY_NCHILDREN];
            int fork_ok = 1;

            for (int ci = 0; ci < LAZY_NCHILDREN; ci++) {
                pids[ci] = fork();
                if (pids[ci] < 0) {
                    fork_ok = 0;
                    for (int j = ci + 1; j < LAZY_NCHILDREN; j++)
                        pids[j] = -1;
                    break;
                }
                if (pids[ci] == 0) {
                    /* Every child writes the same page and checks its own value. */
                    buf[0] = (char)(ci + 1);
                    _exit(buf[0] == (char)(ci + 1) ? 0 : 1);
                }
            }

            TEST(group, "lazy-alloc: all forks succeeded", fork_ok);

            int all_ok = 1;
            for (int ci = 0; ci < LAZY_NCHILDREN; ci++) {
                if (pids[ci] <= 0) continue;
                int status = 0;
                waitpid(pids[ci], &status, 0);
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                    all_ok = 0;
            }
            TEST(group, "lazy-alloc: each child wrote its COW copy without OOM", all_ok);

            /* Parent never wrote buf, so it should still be b. */
            TEST(group, "lazy-alloc: parent buffer unchanged after children wrote",
                 buf[0] == 'b');

            free(buf);
        }
    }
#undef LAZY_NCHILDREN

    ut_group_end(group);
}
