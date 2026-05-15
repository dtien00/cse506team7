/*
 * test_hdr_tcc_builtins.c — include tcc's non-conflicting built-in headers.
 *
 * Covers: float.h  stdalign.h  stdarg.h  stdbool.h  stddef.h  stdnoreturn.h
 * (varargs.h is intentionally #error; tccdefs.h is an internal prelude.)
 *
 * Compile:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_hdr_tcc_builtins.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_hdr_tcc_builtins
 */

#include <float.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdnoreturn.h>
#include <stdio.h>

static void vprint(const char *fmt, va_list ap) {
    vprintf(fmt, ap);
}

static void say(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprint(fmt, ap);
    va_end(ap);
}

int main(void) {
    /* float.h */
    double fmax = DBL_MAX;
    (void)fmax;

    /* stdalign.h */
    _Alignas(16) char aligned_buf[32];
    (void)aligned_buf;

    /* stdarg.h */
    say("test_hdr_tcc_builtins: va_list OK\n");

    /* stdbool.h */
    bool flag = false;
    flag = !flag;
    (void)flag;

    /* stddef.h */
    size_t sz = sizeof(long);
    (void)sz;
    ptrdiff_t pd = 0;
    (void)pd;

    /* stdnoreturn.h — just verify the macro is defined */
#ifndef noreturn
#error "stdnoreturn.h did not define noreturn"
#endif

    printf("PASS\n");
    return 0;
}
