#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)

int printf(const char *fmt, ...);
int fprintf(int fd, const char *fmt, ...);
int dprintf(int fd, const char *fmt, ...);
int putchar(int c);
int puts(const char *s);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);

int vprintf(const char *fmt, va_list ap);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

int sscanf(const char *str, const char *fmt, ...);

void perror(const char *s);

#endif
