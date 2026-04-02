#include <stdarg.h>
#include "kernel/console.h"
#include "kernel/printk.h"
#include "kernel/types.h"

// we maintain a buffer of 128 characters to reduce the number of SBI ecalls
#define PRINTK_BUF_SZ 128

// buffer is flushed to console whenever printk is complete or buffer is full
static void flush(const char *buf, uint64_t *idx) {
    if (*idx) {
        console_write(*idx, buf);
        *idx = 0;
    }
}

// to add a single character to the buffer
static void putc_buf(char *buf, uint64_t *idx, const char c) {
    if (*idx == PRINTK_BUF_SZ)
        flush(buf, idx);
    buf[(*idx)++] = c;
}

// converts int to base 10 for printing as characters
static void put_i64_buf(char *buf, uint64_t *idx, const int64_t i) {
    char tmp[32];
    uint64_t n = 0;
    int t = 0;

    if (i == 0) {
        putc_buf(buf, idx, '0');
        return;
    }
    if (i < 0) {
        putc_buf(buf, idx, '-');
        n = (uint64_t) -i;
    } else
        n = (uint64_t) i;

    // add each digit as a char to tmp in reverse
    while (n && t < 32) {
        tmp[t++] = n % 10 + '0';
        n /= 10;
    }

    // add each char in tmp to buffer in reverse to print digits in the right order
    while (t--)
        putc_buf(buf, idx, tmp[t]);
}

static void put_hex_buf(char *buf, uint64_t *idx, const uint64_t x, int digits) {
    static const char hex[] = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; i--) {
        uint64_t v = (x >> (i * 4)) & 0xFULL;
        putc_buf(buf, idx, hex[v]);
    }
}

void printk_char(char c) {
    console_write(1, &c);
}

void printk(const char *fmt, ...) {
    char buf[PRINTK_BUF_SZ];
    uint64_t idx = 0;

    va_list args;
    va_start(args, fmt);

    for (const char *p = fmt; p && *p; p++) {
        if (*p != '%') {
            putc_buf(buf, &idx, *p);
            continue;
        }
        // if we find a %
        p++;
        if (!*p)
            break;

        switch (*p) {
            case '%':
                putc_buf(buf, &idx, '%');
                break;
            case 'c': {
                const int c = va_arg(args, int);
                putc_buf(buf, &idx, (char) c);
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s)
                    s = "(NULL)";
                while (*s != '\0' && *s)
                    putc_buf(buf, &idx, *s++);
                break;
            }
            case 'd': {
                const int d = va_arg(args, int);
                put_i64_buf(buf, &idx, d);
                break;
            }
            case 'l': {
                // only allow %ld
                if (*(++p) == 'd') {
                    const long v = va_arg(args, long);
                    put_i64_buf(buf, &idx, v);
                } else {
                    putc_buf(buf, &idx, '%');
                    putc_buf(buf, &idx, 'l');
                }
                break;
            }
            case 'p': {
                void *ptr = va_arg(args, void *);
                putc_buf(buf, &idx, '0');
                putc_buf(buf, &idx, 'x');
                put_hex_buf(buf, &idx, (uint64_t) ptr, 16); /* RV64 has 16-byte pointers */
                break;
            }
            default: {
                // Unknown specifier: print it literally
                putc_buf(buf, &idx, '%');
                putc_buf(buf, &idx, *p);
                break;
            }
        }
    }
    va_end(args);
    flush(buf, &idx);
}
