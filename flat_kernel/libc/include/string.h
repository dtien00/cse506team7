#pragma once
#ifndef STRING_H
#define STRING_H
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

static inline void* memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

void* memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

char* strcpy(char *dest, const char *src);
char* strcat(char *dest, const char *src);
char* strncat(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char* strchr(const char *s, int c);
char* strrchr(const char *s, int c);
char* strdup(const char *s);


#endif // STRING_H
