#include "ctype.h"
#include "errno.h"
#include "limits.h"
#include "stdio.h" /* must define FILE with .fd field */
#include "stdlib.h"
#include "string.h"
#include "unistd.h"

enum lenmod { LEN_DEFAULT, LEN_LONG, LEN_LLONG, LEN_SIZE };

struct outctx {
    int fd;
    char *buf;
    int cap;
    int pos;
};

static void fd_write_all(int fd, const char *buf, int len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, (size_t) len);
        if (n <= 0)
            return;
        buf += n;
        len -= (int) n;
    }
}

static void out_char(struct outctx *o, char c) {
    if (o->fd >= 0) {
        fd_write_all(o->fd, &c, 1);
    } else if (o->buf && o->cap > 0 && o->pos < o->cap - 1) {
        o->buf[o->pos] = c;
    }
    o->pos++;
}

static void out_mem(struct outctx *o, const char *s, int n) {
    if (n <= 0)
        return;
    if (o->fd >= 0) {
        fd_write_all(o->fd, s, n);
    } else if (o->buf && o->cap > 0) {
        int avail = o->cap - 1 - o->pos;
        if (avail > 0) {
            int copy = (n < avail) ? n : avail;
            memcpy(o->buf + o->pos, s, (size_t) copy);
        }
    }
    o->pos += n;
}

static void out_pad(struct outctx *o, char ch, int count) {
    while (count-- > 0)
        out_char(o, ch);
}

static int parse_uint(const char **pp) {
    int v = 0;
    const char *p = *pp;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return v;
}

static int ull_to_str(unsigned long long v, unsigned base, int upper, char *buf) {
    static const char digs_lo[] = "0123456789abcdef";
    static const char digs_hi[] = "0123456789ABCDEF";
    const char *digs = upper ? digs_hi : digs_lo;
    char tmp[32];
    int n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v) {
            tmp[n++] = digs[v % base];
            v /= base;
        }
    }
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    return n;
}

static unsigned long long get_unsigned_arg(va_list *ap, enum lenmod len) {
    switch (len) {
        case LEN_LONG:
            return va_arg(*ap, unsigned long);
        case LEN_LLONG:
            return va_arg(*ap, unsigned long long);
        case LEN_SIZE:
            return (unsigned long long) va_arg(*ap, size_t);
        default:
            return (unsigned long long) va_arg(*ap, unsigned int);
    }
}

static long long get_signed_arg(va_list *ap, enum lenmod len) {
    switch (len) {
        case LEN_LONG:
            return va_arg(*ap, long);
        case LEN_LLONG:
            return va_arg(*ap, long long);
        case LEN_SIZE:
            return (long long) va_arg(*ap, ssize_t);
        default:
            return (long long) va_arg(*ap, int);
    }
}

static void format_string(struct outctx *o, const char *s, int width, int prec, int left) {
    if (!s)
        s = "(null)";
    int len = (int) strlen(s);
    if (prec >= 0 && len > prec)
        len = prec;
    if (!left)
        out_pad(o, ' ', width > len ? width - len : 0);
    out_mem(o, s, len);
    if (left)
        out_pad(o, ' ', width > len ? width - len : 0);
}

static void format_char(struct outctx *o, char c, int width, int left) {
    if (!left)
        out_pad(o, ' ', width > 1 ? width - 1 : 0);
    out_char(o, c);
    if (left)
        out_pad(o, ' ', width > 1 ? width - 1 : 0);
}

static void format_uint_common(struct outctx *o, unsigned long long u, unsigned base, int upper, int width, int prec,
                               int left, int zero, const char *prefix, int prefix_len) {
    char digits[32];
    int ndig;
    if (prec == 0 && u == 0) {
        ndig = 0;
    } else {
        ndig = ull_to_str(u, base, upper, digits);
    }

    int zeroes = 0;
    if (prec > ndig)
        zeroes = prec - ndig;
    else if (prec < 0 && zero && !left) {
        int need = width - prefix_len - ndig;
        if (need > 0)
            zeroes = need;
    }

    int total = prefix_len + zeroes + ndig;
    int spaces = (width > total) ? width - total : 0;

    if (!left)
        out_pad(o, ' ', spaces);
    if (prefix_len)
        out_mem(o, prefix, prefix_len);
    out_pad(o, '0', zeroes);
    if (ndig)
        out_mem(o, digits, ndig);
    if (left)
        out_pad(o, ' ', spaces);
}

/* CHANGED: added plus_sign and space_sign parameters */
static void format_int_common(struct outctx *o, long long v, int width, int prec, int left, int zero, int plus_sign,
                              int space_sign) {
    unsigned long long u;
    char sign = 0;

    if (v < 0) {
        sign = '-';
        u = (unsigned long long) (-(v + 1)) + 1ULL;
    } else {
        u = (unsigned long long) v;
        if (plus_sign)
            sign = '+';
        else if (space_sign)
            sign = ' ';
    }

    char prefix[1];
    int prefix_len = 0;
    if (sign) {
        prefix[0] = sign;
        prefix_len = 1;
    }

    format_uint_common(o, u, 10, 0, width, prec, left, zero, prefix, prefix_len);
}

static int vformat(struct outctx *o, const char *fmt, va_list ap) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            out_char(o, *p);
            continue;
        }

        p++;
        if (!*p)
            break;

        /* CHANGED: extended flag loop to handle '+', ' ', '#' */
        int left = 0;
        int zero = 0;
        int plus_sign = 0;
        int space_sign = 0;
        int alt_form = 0;

        int in_flags = 1;
        while (in_flags) {
            switch (*p) {
                case '-':
                    left = 1;
                    p++;
                    break;
                case '0':
                    zero = 1;
                    p++;
                    break;
                case '+':
                    plus_sign = 1;
                    p++;
                    break;
                case ' ':
                    space_sign = 1;
                    p++;
                    break;
                case '#':
                    alt_form = 1;
                    p++;
                    break;
                default:
                    in_flags = 0;
                    break;
            }
        }

        int width = 0;
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            p++;
        } else if (*p >= '0' && *p <= '9') {
            width = parse_uint(&p);
        }

        int prec = -1;
        if (*p == '.') {
            p++;
            if (*p == '*') {
                prec = va_arg(ap, int);
                if (prec < 0)
                    prec = -1;
                p++;
            } else {
                prec = parse_uint(&p);
            }
            zero = 0;
        }

        enum lenmod len = LEN_DEFAULT;
        if (*p == 'l') {
            p++;
            if (*p == 'l') {
                len = LEN_LLONG;
                p++;
            } else
                len = LEN_LONG;
        } else if (*p == 'z') {
            len = LEN_SIZE;
            p++;
        } else if (*p == 'h') {
            p++;
            if (*p == 'h')
                p++; /* hh — promote to int anyway */
        } else if (*p == 'j') {
            len = LEN_LLONG;
            p++;
        }

        switch (*p) {
            case 'd':
            case 'i':
                /* CHANGED: pass plus_sign, space_sign */
                format_int_common(o, get_signed_arg(&ap, len), width, prec, left, zero, plus_sign, space_sign);
                break;

            case 'u':
                format_uint_common(o, get_unsigned_arg(&ap, len), 10, 0, width, prec, left, zero, NULL, 0);
                break;

            case 'x': {
                unsigned long long u = get_unsigned_arg(&ap, len);
                /* CHANGED: '#' alt form emits "0x" prefix */
                const char *pfx = (alt_form && u) ? "0x" : NULL;
                format_uint_common(o, u, 16, 0, width, prec, left, zero, pfx, pfx ? 2 : 0);
                break;
            }

            case 'X': {
                unsigned long long u = get_unsigned_arg(&ap, len);
                /* CHANGED: '#' alt form emits "0X" prefix */
                const char *pfx = (alt_form && u) ? "0X" : NULL;
                format_uint_common(o, u, 16, 1, width, prec, left, zero, pfx, pfx ? 2 : 0);
                break;
            }

            case 'o': {
                unsigned long long u = get_unsigned_arg(&ap, len);
                /* CHANGED: '#' alt form emits leading "0" */
                const char *pfx = (alt_form && u) ? "0" : NULL;
                format_uint_common(o, u, 8, 0, width, prec, left, zero, pfx, pfx ? 1 : 0);
                break;
            }

            case 'p': {
                unsigned long long u = (unsigned long long) (unsigned long) va_arg(ap, void *);
                if (prec < 0)
                    prec = 1;
                format_uint_common(o, u, 16, 0, width, prec, left, 0, "0x", 2);
                break;
            }

            case 'c':
                format_char(o, (char) va_arg(ap, int), width, left);
                break;

            case 's':
                format_string(o, va_arg(ap, const char *), width, prec, left);
                break;

            case 'n': {
                int *np = va_arg(ap, int *);
                if (np)
                    *np = o->pos;
                break;
            }

            case '%':
                format_char(o, '%', width, left);
                break;

            default:
                out_char(o, '%');
                out_char(o, *p);
                break;
        }
    }

    if (o->fd < 0 && o->buf && o->cap > 0) {
        int term = (o->pos < o->cap) ? o->pos : (o->cap - 1);
        o->buf[term] = '\0';
    }

    return o->pos;
}

/* ------------------------------------------------------------------ */
/* errno_string                                                        */
/* ------------------------------------------------------------------ */

static const char *errno_string(int err) {
    switch (err) {
        case 0:
            return "success";
        case EPERM:
            return "operation not permitted";
        case ENOENT:
            return "no such file or directory";
        case ESRCH:
            return "no such process";
        case EINTR:
            return "interrupted system call";
        case EIO:
            return "input/output error";
        case ENXIO:
            return "no such device or address";
        case E2BIG:
            return "argument list too long";
        case ENOEXEC:
            return "exec format error";
        case EBADF:
            return "bad file descriptor";
        case ECHILD:
            return "no child processes";
        case EAGAIN:
            return "resource temporarily unavailable";
        case ENOMEM:
            return "not enough memory";
        case EACCES:
            return "permission denied";
        case EFAULT:
            return "bad address";
        case EBUSY:
            return "device or resource busy";
        case EEXIST:
            return "file exists";
        case ENODEV:
            return "no such device";
        case ENOTDIR:
            return "not a directory";
        case EISDIR:
            return "is a directory";
        case EINVAL:
            return "invalid argument";
        case ENFILE:
            return "too many open files in system";
        case EMFILE:
            return "too many open files";
        case ENOTTY:
            return "inappropriate ioctl for device";
        case EFBIG:
            return "file too large";
        case ENOSPC:
            return "no space left on device";
        case ESPIPE:
            return "illegal seek";
        case EROFS:
            return "read-only file system";
        case EMLINK:
            return "too many links";
        case EPIPE:
            return "broken pipe";
        case EDOM:
            return "math argument out of domain";
        case ERANGE:
            return "result too large";
        case ENAMETOOLONG:
            return "file name too long";
        case ENOSYS:
            return "function not implemented";
        case ENOTEMPTY:
            return "directory not empty";
        case ELOOP:
            return "too many symbolic links";
        case ETIMEDOUT:
            return "connection timed out";
        case ECONNREFUSED:
            return "connection refused";
        default:
            return "error";
    }
}

/* ------------------------------------------------------------------ */
/* Public printf-family API                                            */
/* ------------------------------------------------------------------ */

int vprintf(const char *fmt, va_list ap) {
    struct outctx o = {STDOUT_FILENO, NULL, 0, 0};
    return vformat(&o, fmt, ap);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

/* CHANGED: was fprintf(int fd, ...) — now takes FILE * for busybox compat */
int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    struct outctx o = {stream->fd, NULL, 0, 0};
    return vformat(&o, fmt, ap);
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

/* dprintf stays fd-based — it's a separate POSIX function */
int dprintf(int fd, const char *fmt, ...) {
    struct outctx o = {fd, NULL, 0, 0};
    va_list ap;
    va_start(ap, fmt);
    int n = vformat(&o, fmt, ap);
    va_end(ap);
    return n;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    struct outctx o;
    o.fd = -1;
    o.buf = buf;
    o.cap = (size > (size_t) INT_MAX) ? INT_MAX : (int) size;
    o.pos = 0;
    return vformat(&o, fmt, ap);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    struct outctx o = {-1, buf, INT_MAX, 0};
    return vformat(&o, fmt, ap);
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}

int vasprintf(char **ptr, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return -1;
    char *s = malloc((size_t) n + 1);
    if (!s) return -1;
    vsnprintf(s, (size_t) n + 1, fmt, ap);
    *ptr = s;
    return n;
}

int asprintf(char **ptr, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vasprintf(ptr, fmt, ap);
    va_end(ap);
    return n;
}

int putchar(int c) {
    char ch = (char) c;
    fd_write_all(STDOUT_FILENO, &ch, 1);
    return (unsigned char) ch;
}

int puts(const char *s) {
    int len = (int) strlen(s);
    fd_write_all(STDOUT_FILENO, s, len);
    fd_write_all(STDOUT_FILENO, "\n", 1);
    return len + 1;
}

void perror(const char *s) {
    if (s && *s)
        dprintf(STDERR_FILENO, "%s: %s\n", s, errno_string(errno));
    else
        dprintf(STDERR_FILENO, "%s\n", errno_string(errno));
}

/* ------------------------------------------------------------------ */
/* sscanf                                                              */
/* ------------------------------------------------------------------ */

static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char) *s))
        s++;
    return s;
}

static int scan_token(const char *s, int width, char *buf, int cap) {
    int n = 0;
    while (s[n] && !isspace((unsigned char) s[n]) && (width == 0 || n < width) && n < cap - 1)
        n++;
    memcpy(buf, s, (size_t) n);
    buf[n] = '\0';
    return n;
}

static int store_signed(enum lenmod len, va_list *ap, long long v) {
    switch (len) {
        case LEN_LONG:
            *va_arg(*ap, long *) = (long) v;
            break;
        case LEN_LLONG:
            *va_arg(*ap, long long *) = v;
            break;
        case LEN_SIZE:
            *va_arg(*ap, ssize_t *) = (ssize_t) v;
            break;
        default:
            *va_arg(*ap, int *) = (int) v;
            break;
    }
    return 1;
}

static int store_unsigned(enum lenmod len, va_list *ap, unsigned long long v) {
    switch (len) {
        case LEN_LONG:
            *va_arg(*ap, unsigned long *) = (unsigned long) v;
            break;
        case LEN_LLONG:
            *va_arg(*ap, unsigned long long *) = v;
            break;
        case LEN_SIZE:
            *va_arg(*ap, size_t *) = (size_t) v;
            break;
        default:
            *va_arg(*ap, unsigned int *) = (unsigned int) v;
            break;
    }
    return 1;
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    const char *s = str;
    const char *p = fmt;
    int assigned = 0;

    while (*p) {
        if (isspace((unsigned char) *p)) {
            while (isspace((unsigned char) *p))
                p++;
            s = skip_ws(s);
            continue;
        }

        if (*p != '%') {
            if (*s != *p)
                break;
            s++;
            p++;
            continue;
        }

        p++;
        int suppress = 0;
        if (*p == '*') {
            suppress = 1;
            p++;
        }

        int width = 0;
        if (*p >= '0' && *p <= '9')
            width = parse_uint(&p);

        enum lenmod len = LEN_DEFAULT;
        if (*p == 'l') {
            p++;
            if (*p == 'l') {
                len = LEN_LLONG;
                p++;
            } else
                len = LEN_LONG;
        } else if (*p == 'z') {
            len = LEN_SIZE;
            p++;
        }

        if (*p == '%') {
            if (*s != '%')
                break;
            s++;
            p++;
            continue;
        }

        if (*p == 'c') {
            int n = width ? width : 1;
            for (int i = 0; i < n; i++)
                if (!s[i]) {
                    va_end(ap);
                    return assigned;
                }
            if (!suppress) {
                char *dst = va_arg(ap, char *);
                for (int i = 0; i < n; i++)
                    dst[i] = s[i];
                assigned++;
            }
            s += n;
            p++;
            continue;
        }

        if (*p == 's') {
            s = skip_ws(s);
            if (!*s)
                break;
            int n = 0;
            while (s[n] && !isspace((unsigned char) s[n]) && (width == 0 || n < width))
                n++;
            if (n == 0)
                break;
            if (!suppress) {
                char *dst = va_arg(ap, char *);
                memcpy(dst, s, (size_t) n);
                dst[n] = '\0';
                assigned++;
            }
            s += n;
            p++;
            continue;
        }

        s = skip_ws(s);
        if (!*s)
            break;

        char tmp[128];
        int n = scan_token(s, width, tmp, sizeof(tmp));
        if (n == 0)
            break;

        char *end = NULL;

        switch (*p) {
            case 'd':
            case 'i': {
                long long v = strtoll(tmp, &end, (*p == 'i') ? 0 : 10);
                if (end == tmp) {
                    va_end(ap);
                    return assigned;
                }
                if (!suppress)
                    assigned += store_signed(len, &ap, v);
                s += (end - tmp);
                break;
            }
            case 'u': {
                unsigned long long v = strtoull(tmp, &end, 10);
                if (end == tmp) {
                    va_end(ap);
                    return assigned;
                }
                if (!suppress)
                    assigned += store_unsigned(len, &ap, v);
                s += (end - tmp);
                break;
            }
            case 'x':
            case 'X': {
                unsigned long long v = strtoull(tmp, &end, 16);
                if (end == tmp) {
                    va_end(ap);
                    return assigned;
                }
                if (!suppress)
                    assigned += store_unsigned(len, &ap, v);
                s += (end - tmp);
                break;
            }
            case 'o': {
                /* CHANGED: added missing octal scan case */
                unsigned long long v = strtoull(tmp, &end, 8);
                if (end == tmp) {
                    va_end(ap);
                    return assigned;
                }
                if (!suppress)
                    assigned += store_unsigned(len, &ap, v);
                s += (end - tmp);
                break;
            }
            case 'p': {
                unsigned long long v = strtoull(tmp, &end, 16);
                if (end == tmp) {
                    va_end(ap);
                    return assigned;
                }
                if (!suppress) {
                    void **dst = va_arg(ap, void **);
                    *dst = (void *) (unsigned long) v;
                    assigned++;
                }
                s += (end - tmp);
                break;
            }
            default:
                va_end(ap);
                return assigned;
        }

        p++;
    }

    va_end(ap);
    return assigned;
}
