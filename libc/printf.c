#include <stdarg.h>

#define SYS_write 4

static void do_write(const char *buf, int len) {
    if (len <= 0) return;
    register long a0 asm("a0") = 1;           /* fd = stdout */
    register long a1 asm("a1") = (long)buf;
    register long a2 asm("a2") = len;
    register long a7 asm("a7") = SYS_write;
    asm volatile("ecall" :: "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int written = 0;

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            do_write(p, 1);
            written++;
            continue;
        }
        p++;  /* skip '%' */
        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len]) len++;
            do_write(s, len);
            written += len;
            break;
        }
        case 'd': {
            int v = va_arg(ap, int);
            char buf[20];
            int i = 19;
            buf[i] = '\0';
            int neg = (v < 0);
            unsigned int u = neg ? (unsigned int)(-v) : (unsigned int)v;
            if (u == 0) {
                buf[--i] = '0';
            } else {
                while (u) { buf[--i] = '0' + (int)(u % 10); u /= 10; }
            }
            if (neg) buf[--i] = '-';
            int len = 19 - i;
            do_write(&buf[i], len);
            written += len;
            break;
        }
        case '%':
            do_write("%", 1);
            written++;
            break;
        default:
            do_write("%", 1);
            do_write(p, 1);
            written += 2;
            break;
        }
    }

    va_end(ap);
    return written;
}
