#include "string.h"
#include "stddef.h"
#include "stdlib.h"
#include "signal.h"

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

char* strerror(int errnum) {
    (void)errnum;
    return "error";
}

size_t strlcpy(char *dest, const char *src, size_t size) {
    size_t srclen = 0;
    while (src[srclen]) srclen++;
    if (size) {
        size_t n = (srclen < size - 1) ? srclen : size - 1;
        for (size_t i = 0; i < n; i++) dest[i] = src[i];
        dest[n] = '\0';
    }
    return srclen;
}

size_t strlcat(char *dest, const char *src, size_t size) {
    size_t dlen = 0;
    while (dlen < size && dest[dlen]) dlen++;
    size_t srclen = 0;
    while (src[srclen]) srclen++;
    if (dlen == size) return size + srclen;
    size_t n = srclen;
    if (n > size - dlen - 1) n = size - dlen - 1;
    for (size_t i = 0; i < n; i++) dest[dlen + i] = src[i];
    dest[dlen + n] = '\0';
    return dlen + srclen;
}

int strerror_r(int errnum, char *buf, size_t buflen) {
    char *msg = strerror(errnum);
    size_t mlen = strlen(msg);
    if (buflen == 0) return 34 /* ERANGE */;
    size_t n = (mlen < buflen - 1) ? mlen : buflen - 1;
    for (size_t i = 0; i < n; i++) buf[i] = msg[i];
    buf[n] = '\0';
    return (mlen >= buflen) ? 34 /* ERANGE */ : 0;
}

char* strsignal(int signum) {
    static char unknown[24];
    switch (signum) {
        case SIGHUP:    return "Hangup";
        case SIGINT:    return "Interrupt";
        case SIGQUIT:   return "Quit";
        case SIGILL:    return "Illegal instruction";
        case SIGTRAP:   return "Trace/breakpoint trap";
        case SIGABRT:   return "Aborted";
        case SIGBUS:    return "Bus error";
        case SIGFPE:    return "Floating point exception";
        case SIGKILL:   return "Killed";
        case SIGUSR1:   return "User defined signal 1";
        case SIGSEGV:   return "Segmentation fault";
        case SIGUSR2:   return "User defined signal 2";
        case SIGPIPE:   return "Broken pipe";
        case SIGALRM:   return "Alarm clock";
        case SIGTERM:   return "Terminated";
        case SIGCHLD:   return "Child exited";
        case SIGCONT:   return "Continued";
        case SIGSTOP:   return "Stopped (signal)";
        case SIGTSTP:   return "Stopped";
        case SIGTTIN:   return "Stopped (tty input)";
        case SIGTTOU:   return "Stopped (tty output)";
        case SIGURG:    return "Urgent I/O condition";
        case SIGXCPU:   return "CPU time limit exceeded";
        case SIGXFSZ:   return "File size limit exceeded";
        case SIGVTALRM: return "Virtual timer expired";
        case SIGPROF:   return "Profiling timer expired";
        case SIGWINCH:  return "Window changed";
        case SIGIO:     return "I/O possible";
        case SIGPWR:    return "Power failure";
        case SIGSYS:    return "Bad system call";
    }
    const char *p = "Unknown signal ";
    size_t i = 0;
    while (p[i] && i < sizeof(unknown) - 4) { unknown[i] = p[i]; i++; }
    int n = signum;
    char d[12];
    int dn = 0;
    if (n < 0) { unknown[i++] = '-'; n = -n; }
    if (n == 0) d[dn++] = '0';
    while (n > 0 && dn < 11) { d[dn++] = '0' + (n % 10); n /= 10; }
    while (dn > 0 && i < sizeof(unknown) - 1) unknown[i++] = d[--dn];
    unknown[i] = '\0';
    return unknown;
}