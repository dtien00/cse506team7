#pragma once
#include <stdarg.h>
#include "stddef.h"

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

int printf(const char *fmt, ...);
int fprintf(int fd, const char *fmt, ...);
int dprintf(int fd, const char *fmt, ...);
int sprintf(char *str, const char *fmt, ...);
int snprintf(char *str, size_t size, const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int vfprintf(int fd, const char *fmt, va_list ap);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

int putchar(int c);
int puts(const char *s);
int getchar(void);
