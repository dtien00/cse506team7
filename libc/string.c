#include "string.h"
#include "stddef.h"
#include "stdlib.h"

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

static const char *const _sig_strings[] = {
    /* [0]  unused      */ NULL,
    /* [1]  SIGHUP      */ "Hangup",
    /* [2]  SIGINT      */ "Interrupt",
    /* [3]  SIGQUIT     */ "Quit",
    /* [4]  SIGILL      */ "Illegal instruction",
    /* [5]  SIGTRAP     */ "Trace/breakpoint trap",
    /* [6]  SIGABRT     */ "Aborted",
    /* [7]  SIGBUS      */ "Bus error",
    /* [8]  SIGFPE      */ "Floating point exception",
    /* [9]  SIGKILL     */ "Killed",
    /* [10] SIGUSR1     */ "User defined signal 1",
    /* [11] SIGSEGV     */ "Segmentation fault",
    /* [12] SIGUSR2     */ "User defined signal 2",
    /* [13] SIGPIPE     */ "Broken pipe",
    /* [14] SIGALRM     */ "Alarm clock",
    /* [15] SIGTERM     */ "Terminated",
    /* [16] SIGSTKFLT   */ "Stack fault",
    /* [17] SIGCHLD     */ "Child exited",
    /* [18] SIGCONT     */ "Continued",
    /* [19] SIGSTOP     */ "Stopped (signal)",
    /* [20] SIGTSTP     */ "Stopped",
    /* [21] SIGTTIN     */ "Stopped (tty input)",
    /* [22] SIGTTOU     */ "Stopped (tty output)",
    /* [23] SIGURG      */ "Urgent I/O condition",
    /* [24] SIGXCPU     */ "CPU time limit exceeded",
    /* [25] SIGXFSZ     */ "File size limit exceeded",
    /* [26] SIGVTALRM   */ "Virtual timer expired",
    /* [27] SIGPROF     */ "Profiling timer expired",
    /* [28] SIGWINCH    */ "Window changed",
    /* [29] SIGIO       */ "I/O possible",
    /* [30] SIGPWR      */ "Power failure",
    /* [31] SIGSYS      */ "Bad system call",
};

#define _NSIG_STRINGS ((int)(sizeof(_sig_strings) / sizeof(_sig_strings[0])))

char *strsignal(int signum) {
    if (signum > 0 && signum < _NSIG_STRINGS)
        return (char *)_sig_strings[signum];

    // Build "Unknown signal <N>" into a static buffer for out-of-range values.
    static char _unknown_buf[32];
    const char *prefix = "Unknown signal ";
    char *p = _unknown_buf;
    for (const char *s = prefix; *s; s++)
        *p++ = *s;

    // Write decimal digits of |signum|.
    unsigned int u = (signum < 0) ? (unsigned int)(-signum) : (unsigned int)signum;
    char digits[12];
    int nd = 0;
    if (u == 0) {
        digits[nd++] = '0';
    } else {
        while (u > 0) {
            digits[nd++] = '0' + (u % 10);
            u /= 10;
        }
    }
    for (int i = nd - 1; i >= 0; i--)
        *p++ = digits[i];
    *p = '\0';
    return _unknown_buf;
}