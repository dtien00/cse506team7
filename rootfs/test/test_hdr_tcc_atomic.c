/*
 * test_hdr_tcc_atomic.c — include stdatomic.h and exercise atomic operations.
 * Kept separate because stdatomic.h re-includes stdint.h/stdbool.h internally
 * and conflicts if mixed with a full libc include set.
 *
 * Compile:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_hdr_tcc_atomic.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_hdr_tcc_atomic
 *
 * Note: only use atomic_fetch_* and atomic_flag ops; the _explicit store/load
 * macros use GNU statement-expressions ({ }) which TCC does not support.
 */

#include <stdatomic.h>
#include <stdio.h>

int main(void) {
    atomic_int counter = 0;

    /* Use fetch_add/fetch_sub to set and read values — avoids ({ }) macros */
    atomic_fetch_add(&counter, 42);
    int v = atomic_fetch_add(&counter, 0); /* read by adding 0 */
    if (v != 42) {
        printf("FAIL: initial set: got %d expected 42\n", v);
        return 1;
    }

    int old = atomic_fetch_add(&counter, 8);
    if (old != 42) {
        printf("FAIL: atomic_fetch_add old: got %d expected 42\n", old);
        return 1;
    }
    v = atomic_fetch_add(&counter, 0);
    if (v != 50) {
        printf("FAIL: after fetch_add: got %d expected 50\n", v);
        return 1;
    }

    atomic_fetch_sub(&counter, 50);
    v = atomic_fetch_add(&counter, 0);
    if (v != 0) {
        printf("FAIL: after fetch_sub: got %d expected 0\n", v);
        return 1;
    }

    /* memory_order enum defined in stdatomic.h — just verify it compiles */
    (void)memory_order_relaxed;
    (void)memory_order_acquire;
    (void)memory_order_release;
    (void)memory_order_seq_cst;

    /* atomic_flag round-trip */
    atomic_flag f = ATOMIC_FLAG_INIT;
    int set = atomic_flag_test_and_set(&f);
    if (set != 0) {
        printf("FAIL: flag first test_and_set: got %d expected 0\n", set);
        return 1;
    }
    set = atomic_flag_test_and_set(&f);
    if (set == 0) {
        printf("FAIL: flag second test_and_set should be non-zero\n");
        return 1;
    }
    atomic_flag_clear(&f);
    set = atomic_flag_test_and_set(&f);
    if (set != 0) {
        printf("FAIL: flag after clear: got %d expected 0\n", set);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
