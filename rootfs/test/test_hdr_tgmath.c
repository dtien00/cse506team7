/*
 * test_hdr_tgmath.c — include tgmath.h and exercise _Generic-based math macros.
 *
 * Compile:
 *   cc -nostdlib -nostdinc -I/include -I/lib/tcc/include \
 *      /test/test_hdr_tgmath.c /lib/crt.o /lib/libc.a \
 *      /lib/tcc/libtcc1.a -o /tmp/test_hdr_tgmath
 */

#include <tgmath.h>
#include <stdio.h>

int main(void) {
    /* tgmath.h wraps math.h functions with _Generic so the right
     * float/double/long-double variant is called automatically. */

    /* math.h on this target is a soft-float stub: only double variants exist.
     * tgmath.h's _Generic selects the double path for double arguments. */
    double dv = sqrt(4.0);
    if (dv < 1.99 || dv > 2.01) {
        int dv_i = (int)dv; int dv_f = (int)((dv - dv_i) * 1000000);
        printf("FAIL: sqrt(4.0) = %d.%06d\n", dv_i, dv_f);
        return 1;
    }

    double dv2 = sqrt(9.0);
    if (dv2 < 2.99 || dv2 > 3.01) {
        int dv2_i = (int)dv2; int dv2_f = (int)((dv2 - dv2_i) * 1000000);
        printf("FAIL: sqrt(9.0) = %d.%06d\n", dv2_i, dv2_f);
        return 1;
    }

    double av = fabs(-7.5);
    if (av < 7.49 || av > 7.51) {
        int av_i = (int)av; int av_f = (int)((av - av_i) * 1000000);
        printf("FAIL: fabs(-7.5) = %d.%06d\n", av_i, av_f);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
