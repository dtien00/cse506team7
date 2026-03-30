#include <stdarg.h>
#include <stddef.h>
#include <limits.h>

#define SYS_write 4

static void do_write(const char *buf, int len) {
    if (len <= 0) return;
    register long a0 asm("a0") = 1;           /* fd = stdout */
    register long a1 asm("a1") = (long)buf;
    register long a2 asm("a2") = len;
    register long a7 asm("a7") = SYS_write;
    asm volatile("ecall" :: "r"(a0), "r"(a1), "r"(a2), "r"(a7) : "memory");
}

/* ------------------------------------------------------------------ */
/* Buffer output context used by snprintf/sprintf                      */
/* ------------------------------------------------------------------ */

struct bufctx {
    char *buf;
    int   cap;  /* total capacity (room for NUL) */
    int   pos;  /* chars written so far           */
};

/* Write one char to a bufctx; always counts toward pos even if cap exceeded */
static void bctx_putc(struct bufctx *b, char c) {
    if (b->pos < b->cap - 1)
        b->buf[b->pos] = c;
    b->pos++;
}

static void bctx_puts_n(struct bufctx *b, const char *s, int n) {
    for (int i = 0; i < n; i++)
        bctx_putc(b, s[i]);
}

/* ------------------------------------------------------------------ */
/* Shared formatting core — NULL bufctx means write to stdout          */
/* ------------------------------------------------------------------ */

static int vformat(struct bufctx *b, const char *fmt, va_list ap) {
    int written = 0;

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            if (b) bctx_putc(b, *p);
            else   do_write(p, 1);
            written++;
            continue;
        }
        p++;  /* skip '%' */

        /* Check for 'l' length modifier */
        int long_flag = 0;
        if (*p == 'l') {
            long_flag = 1;
            p++;
        }

        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len]) len++;
            if (b) bctx_puts_n(b, s, len);
            else   do_write(s, len);
            written += len;
            break;
        }
        case 'd': {
            long v = long_flag ? va_arg(ap, long) : (long)va_arg(ap, int);
            char buf[22];
            int i = 21;
            buf[i] = '\0';
            int neg = (v < 0);
            unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;
            if (u == 0) {
                buf[--i] = '0';
            } else {
                while (u) { buf[--i] = '0' + (int)(u % 10); u /= 10; }
            }
            if (neg) buf[--i] = '-';
            int len = 21 - i;
            if (b) bctx_puts_n(b, &buf[i], len);
            else   do_write(&buf[i], len);
            written += len;
            break;
        }
        case 'u': {
            unsigned long u = long_flag ? va_arg(ap, unsigned long)
                                        : (unsigned long)va_arg(ap, unsigned int);
            char buf[22];
            int i = 21;
            buf[i] = '\0';
            if (u == 0) {
                buf[--i] = '0';
            } else {
                while (u) { buf[--i] = '0' + (int)(u % 10); u /= 10; }
            }
            int len = 21 - i;
            if (b) bctx_puts_n(b, &buf[i], len);
            else   do_write(&buf[i], len);
            written += len;
            break;
        }
        case 'x':
        case 'p': {
            unsigned long u;
            int prefix = (*p == 'p');
            if (prefix)
                u = (unsigned long)va_arg(ap, void *);
            else
                u = long_flag ? va_arg(ap, unsigned long)
                              : (unsigned long)va_arg(ap, unsigned int);
            char buf[20];
            int i = 19;
            buf[i] = '\0';
            if (u == 0) {
                buf[--i] = '0';
            } else {
                while (u) {
                    int d = (int)(u & 0xf);
                    buf[--i] = (d < 10) ? ('0' + d) : ('a' + d - 10);
                    u >>= 4;
                }
            }
            int len = 19 - i;
            if (prefix) {
                if (b) { bctx_putc(b, '0'); bctx_putc(b, 'x'); }
                else   do_write("0x", 2);
                written += 2;
            }
            if (b) bctx_puts_n(b, &buf[i], len);
            else   do_write(&buf[i], len);
            written += len;
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (b) bctx_putc(b, c);
            else   do_write(&c, 1);
            written++;
            break;
        }
        case '%':
            if (b) bctx_putc(b, '%');
            else   do_write("%", 1);
            written++;
            break;
        default:
            if (b) { bctx_putc(b, '%'); bctx_putc(b, *p); }
            else   { do_write("%", 1); do_write(p, 1); }
            written += 2;
            break;
        }
    }

    return written;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vformat(NULL, fmt, ap);
    va_end(ap);
    return n;
}

int putchar(int c) {
    char ch = (char)c;
    do_write(&ch, 1);
    return (unsigned char)ch;
}

int puts(const char *s) {
    int len = 0;
    while (s[len]) len++;
    do_write(s, len);
    do_write("\n", 1);
    return len + 1;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    if (!buf || size == 0) return 0;
    struct bufctx b = { buf, (int)size, 0 };
    va_list ap;
    va_start(ap, fmt);
    int n = vformat(&b, fmt, ap);
    va_end(ap);
    /* NUL-terminate within capacity */
    int term = (b.pos < (int)size) ? b.pos : (int)size - 1;
    buf[term] = '\0';
    return n;
}

int sprintf(char *buf, const char *fmt, ...) {
    struct bufctx b = { buf, INT_MAX, 0 };
    va_list ap;
    va_start(ap, fmt);
    int n = vformat(&b, fmt, ap);
    va_end(ap);
    buf[b.pos] = '\0';
    return n;
}
