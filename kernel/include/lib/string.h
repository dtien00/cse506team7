
#pragma once
#include "types.h"

static inline void* memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static inline int strncpy(char *dest, const char *src, size_t n) {
    while (n-- && (*dest++ = *src++));
    return 0;
}

static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}
