#include "string.h"

void* memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void* memset(void *dest, int c, size_t n) {
    uint8_t *d = dest;
    while (n--) *d++ = (uint8_t)c;
    return dest;
}

int strncpy(char *dest, const char *src, size_t n) {
    while (n-- && (*dest++ = *src++));
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
