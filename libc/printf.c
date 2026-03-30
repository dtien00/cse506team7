#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

static void do_write_fd(int fd, const char *buf, int len) {
    if (len <= 0) return;
    write(fd, buf, len);
}

static int vfprintf_internal(int fd, const char *fmt, va_list ap) {
    int written = 0;

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            do_write_fd(fd, p, 1);
            written++;
            continue;
        }
        p++;
        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len]) len++;
            do_write_fd(fd, s, len);
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
            do_write_fd(fd, &buf[i], len);
            written += len;
            break;
        }
        case 'x': {
            unsigned int v = va_arg(ap, unsigned int);
            char buf[20];
            int i = 19;
            buf[i] = '\0';
            if (v == 0) {
                buf[--i] = '0';
            } else {
                while (v) {
                    int digit = v % 16;
                    buf[--i] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
                    v /= 16;
                }
            }
            int len = 19 - i;
            do_write_fd(fd, &buf[i], len);
            written += len;
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            do_write_fd(fd, &c, 1);
            written++;
            break;
        }
        case '%':
            do_write_fd(fd, "%", 1);
            written++;
            break;
        default:
            do_write_fd(fd, "%", 1);
            do_write_fd(fd, p, 1);
            written += 2;
            break;
        }
    }

    return written;
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf_internal(STDOUT_FILENO, fmt, ap);
    va_end(ap);
    return ret;
}

int fprintf(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf_internal(fd, fmt, ap);
    va_end(ap);
    return ret;
}

int dprintf(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf_internal(fd, fmt, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *fmt, va_list ap) {
    return vfprintf_internal(STDOUT_FILENO, fmt, ap);
}

int vfprintf(int fd, const char *fmt, va_list ap) {
    return vfprintf_internal(fd, fmt, ap);
}

int putchar(int c) {
    char ch = (char)c;
    return (write(STDOUT_FILENO, &ch, 1) == 1) ? c : -1;
}

int puts(const char *s) {
    if (!s) return -1;
    int len = 0;
    while (s[len]) len++;
    write(STDOUT_FILENO, s, len);
    write(STDOUT_FILENO, "\n", 1);
    return len + 1;
}

int getchar(void) {
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n <= 0) {
        return -1;
    }
    return c;
}

static int vsnprintf_internal(char *str, size_t size, const char *fmt, va_list ap) {
    if (!str || size == 0) return 0;
    
    size_t pos = 0;
    int written = 0;

    for (const char *p = fmt; *p && pos < size - 1; p++) {
        if (*p != '%') {
            str[pos++] = *p;
            written++;
            continue;
        }
        p++;
        switch (*p) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && pos < size - 1) {
                str[pos++] = *s++;
                written++;
            }
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
            for (int j = i; j < 19 && pos < size - 1; j++) {
                str[pos++] = buf[j];
                written++;
            }
            break;
        }
        case 'x': {
            unsigned int v = va_arg(ap, unsigned int);
            char buf[20];
            int i = 19;
            buf[i] = '\0';
            if (v == 0) {
                buf[--i] = '0';
            } else {
                while (v) {
                    int digit = v % 16;
                    buf[--i] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
                    v /= 16;
                }
            }
            for (int j = i; j < 19 && pos < size - 1; j++) {
                str[pos++] = buf[j];
                written++;
            }
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (pos < size - 1) {
                str[pos++] = c;
                written++;
            }
            break;
        }
        case '%':
            if (pos < size - 1) {
                str[pos++] = '%';
                written++;
            }
            break;
        default:
            if (pos < size - 1) str[pos++] = '%';
            if (pos < size - 1) str[pos++] = *p;
            written += 2;
            break;
        }
    }

    str[pos] = '\0';
    return written;
}

int sprintf(char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf_internal(str, (size_t)-1, fmt, ap);
    va_end(ap);
    return ret;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf_internal(str, size, fmt, ap);
    va_end(ap);
    return ret;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    return vsnprintf_internal(str, size, fmt, ap);
}
