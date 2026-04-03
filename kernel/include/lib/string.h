
#pragma once
#include "kernel/types.h"

void* memcpy(void *dest, const void *src, size_t n);
void* memset(void *dest, int c, size_t n);
char* strcpy(char *dest, const char *src);
int strncpy(char *dest, const char *src, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);