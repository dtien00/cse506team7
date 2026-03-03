#pragma once

#include "stdint.h"

static inline void* memcpy(void *dest, const void *src, unsigned long n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static inline int strncpy(char *dest, const char *src, unsigned long n) {
    while (n-- && (*dest++ = *src++));
    return 0;
}

static inline unsigned long strlen(const char *s) {
    unsigned long len = 0;
    while (*s++) len++;
    return len;
}