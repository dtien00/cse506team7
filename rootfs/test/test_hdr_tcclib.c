/*
 * test_hdr_tcclib.c — include tcclib.h (tcc's minimal libc stub).
 * Must be compiled WITHOUT including stdio.h/stdlib.h/string.h since
 * tcclib.h re-declares those functions and will conflict.
 *
 * Compile:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_hdr_tcclib.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_hdr_tcclib
 */

#include <tcclib.h>

int main(void) {
    /* malloc/free from tcclib.h */
    void *p = malloc(64);
    if (!p) {
        printf("FAIL: malloc returned NULL\n");
        return 1;
    }

    /* memset/strlen from tcclib.h */
    memset(p, 'x', 64);
    char *s = (char *)p;
    s[63] = '\0';
    size_t len = strlen(s);
    if (len != 63) {
        printf("FAIL: strlen got %zu expected 63\n", len);
        free(p);
        return 1;
    }

    free(p);

    /* atoi from tcclib.h */
    int n = atoi("1234");
    if (n != 1234) {
        printf("FAIL: atoi got %d expected 1234\n", n);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
