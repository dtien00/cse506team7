#include "string.h"
#include "stddef.h"
#include "stdlib.h"
#include "errno.h"

void* memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void* memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

char* strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n-- && (*d++ = *src++));
    while (n-- != (size_t)-1) *d++ = '\0';
    return dest;
}

void* memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;

    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

char* strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char* strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    while (n-- && (*d++ = *src++));
    if (n == (size_t)-1) *d = '\0';
    return dest;
}

size_t strlcpy(char *dest, const char *src, size_t size) {
    const char *s = src;
    if (size) {
        char *d = dest;
        size_t n = size - 1;
        while (n && *s) { *d++ = *s++; n--; }
        *d = '\0';
    }
    while (*s) s++;
    return (size_t)(s - src);
}

size_t strlcat(char *dest, const char *src, size_t size) {
    char *d = dest;
    const char *s = src;
    size_t n = size;
    while (n-- && *d) d++;
    size_t dlen = (size_t)(d - dest);
    n = size - dlen;
    if (n == 0) {
        while (*s) s++;
        return dlen + (size_t)(s - src);
    }
    while (*s) {
        if (n > 1) { *d++ = *s; n--; }
        s++;
    }
    *d = '\0';
    return dlen + (size_t)(s - src);
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const uint8_t*)s1 - *(const uint8_t*)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const uint8_t*)s1 - *(const uint8_t*)s2;
}

char* strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

char* strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return (char*)last;
}

char* strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *new_str = malloc(len);
    if (new_str) {
        memcpy(new_str, s, len);
    }
    return new_str;
}

char* strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;

    size_t needle_len = strlen(needle);
    while (*haystack) {
        if (*haystack == *needle && strncmp(haystack, needle, needle_len) == 0) {
            return (char*)haystack;
        }
        haystack++;
    }
    return NULL;
}

void* memchr(const void *s, int c, size_t n) {
    const uint8_t *p = s;
    while (n--) {
        if (*p == (uint8_t)c) return (void*)p;
        p++;
    }
    return NULL;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    while (len < maxlen && s[len]) len++;
    return len;
}

char* strtok(char *str, const char *delim) {
    static char *saved = NULL;
    if (str) saved = str;
    if (!saved || !*saved) return NULL;

    /* skip leading delimiters */
    while (*saved && strchr(delim, *saved)) saved++;
    if (!*saved) return NULL;

    char *start = saved;
    while (*saved && !strchr(delim, *saved)) saved++;
    if (*saved) {
        *saved = '\0';
        saved++;
    } else {
        saved = NULL;
    }
    return start;
}

static const char *errstr(int e) {
    switch (e) {
    case EPERM:           return "Operation not permitted";
    case ENOENT:          return "No such file or directory";
    case ESRCH:           return "No such process";
    case EINTR:           return "Interrupted system call";
    case EIO:             return "Input/output error";
    case ENXIO:           return "No such device or address";
    case E2BIG:           return "Argument list too long";
    case ENOEXEC:         return "Exec format error";
    case EBADF:           return "Bad file descriptor";
    case ECHILD:          return "No child processes";
    case EAGAIN:          return "Resource temporarily unavailable";
    case ENOMEM:          return "Out of memory";
    case EACCES:          return "Permission denied";
    case EFAULT:          return "Bad address";
    case ENOTBLK:         return "Block device required";
    case EBUSY:           return "Device or resource busy";
    case EEXIST:          return "File exists";
    case EXDEV:           return "Invalid cross-device link";
    case ENODEV:          return "No such device";
    case ENOTDIR:         return "Not a directory";
    case EISDIR:          return "Is a directory";
    case EINVAL:          return "Invalid argument";
    case ENFILE:          return "Too many open files in system";
    case EMFILE:          return "Too many open files";
    case ENOTTY:          return "Inappropriate ioctl for device";
    case ETXTBSY:         return "Text file busy";
    case EFBIG:           return "File too large";
    case ENOSPC:          return "No space left on device";
    case ESPIPE:          return "Illegal seek";
    case EROFS:           return "Read-only file system";
    case EMLINK:          return "Too many links";
    case EPIPE:           return "Broken pipe";
    case EDOM:            return "Numerical argument out of domain";
    case ERANGE:          return "Numerical result out of range";
    case EDEADLK:         return "Resource deadlock avoided";
    case ENAMETOOLONG:    return "File name too long";
    case ENOLCK:          return "No locks available";
    case ENOSYS:          return "Function not implemented";
    case ENOTEMPTY:       return "Directory not empty";
    case ELOOP:           return "Too many levels of symbolic links";
    case ENOMSG:          return "No message of desired type";
    case EIDRM:           return "Identifier removed";
    case ENOLINK:         return "Link has been severed";
    case EPROTO:          return "Protocol error";
    case EMULTIHOP:       return "Multihop attempted";
    case EBADMSG:         return "Bad message";
    case EOVERFLOW:       return "Value too large for defined data type";
    case EILSEQ:          return "Invalid or incomplete multibyte character";
    case ENOTSOCK:        return "Socket operation on non-socket";
    case EDESTADDRREQ:    return "Destination address required";
    case EMSGSIZE:        return "Message too long";
    case EPROTOTYPE:      return "Protocol wrong type for socket";
    case ENOPROTOOPT:     return "Protocol not available";
    case EPROTONOSUPPORT: return "Protocol not supported";
    case ESOCKTNOSUPPORT: return "Socket type not supported";
    case EOPNOTSUPP:      return "Operation not supported";
    case EAFNOSUPPORT:    return "Address family not supported by protocol";
    case EADDRINUSE:      return "Address already in use";
    case EADDRNOTAVAIL:   return "Cannot assign requested address";
    case ENETDOWN:        return "Network is down";
    case ENETUNREACH:     return "Network is unreachable";
    case ENETRESET:       return "Network dropped connection on reset";
    case ECONNABORTED:    return "Software caused connection abort";
    case ECONNRESET:      return "Connection reset by peer";
    case ENOBUFS:         return "No buffer space available";
    case EISCONN:         return "Transport endpoint is already connected";
    case ENOTCONN:        return "Transport endpoint is not connected";
    case ETIMEDOUT:       return "Connection timed out";
    case ECONNREFUSED:    return "Connection refused";
    case EHOSTUNREACH:    return "No route to host";
    case EALREADY:        return "Operation already in progress";
    case EINPROGRESS:     return "Operation now in progress";
    case ESTALE:          return "Stale file handle";
    case EDQUOT:          return "Disk quota exceeded";
    case ECANCELED:       return "Operation canceled";
    case EOWNERDEAD:      return "Owner died";
    case ENOTRECOVERABLE: return "State not recoverable";
    default:              return NULL;
    }
}

char* strerror(int errnum) {
    // (void)errnum;
    // return "error";
    const char *s = errstr(errnum);
    return (char *)(s ? s : "Unknown error");
}


int strerror_r(int errnum, char *buf, size_t buflen) {
    if (buflen == 0) return ERANGE;
    const char *msg = errstr(errnum);
    int rc = 0;
    if (!msg) {
        msg = "Unknown error";
        rc = EINVAL;
    }
    size_t len = strlen(msg);
    if (len >= buflen) {
        strncpy(buf, msg, buflen - 1);
        buf[buflen - 1] = '\0';
        return ERANGE;
    }
    memcpy(buf, msg, len + 1);
    return rc;
}
