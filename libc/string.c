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

char* strerror(int errnum) {
    (void)errnum;
    return "error";
}