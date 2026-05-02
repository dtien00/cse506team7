#ifndef _STDIO_H
#define _STDIO_H
#include "stddef.h"
#include "stdarg.h"

#define EOF (-1)
#define BUFSIZ 256

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* Opaque FILE type backed by an fd + small buffer */
typedef struct {
    int  fd;
    int  eof;
    int  error;
    int  writable;
    char buf[256];   /* read buffer OR write buffer — not both at once */
    int  buf_pos;    /* write: bytes pending flush / read: next unread byte */
    int  buf_len;    /* read: valid bytes in buf (0 for write streams)      */
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE   *fopen(const char *path, const char *mode);
int     fclose(FILE *stream);
size_t  fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t  fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int     fgetc(FILE *stream);
int     fputc(int c, FILE *stream);
char   *fgets(char *s, int n, FILE *stream);
int     fputs(const char *s, FILE *stream);
int     feof(FILE *stream);
int     ferror(FILE *stream);
int     fflush(FILE *stream);
int     fseek(FILE *stream, long offset, int whence);
long    ftell(FILE *stream);
void    rewind(FILE *stream);
int     fileno(FILE *stream);

/* These must take FILE*, NOT int */
int     fprintf(FILE *stream, const char *fmt, ...);
int     vfprintf(FILE *stream, const char *fmt, va_list ap);

/* fd-level (keep these separately) */
int     printf(const char *fmt, ...);
int     vprintf(const char *fmt, va_list ap);
int     sprintf(char *buf, const char *fmt, ...);
int     snprintf(char *buf, size_t size, const char *fmt, ...);
int     vsprintf(char *buf, const char *fmt, va_list ap);
int     vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int     dprintf(int fd, const char *fmt, ...);
int     sscanf(const char *str, const char *fmt, ...);
int     putchar(int c);
int     puts(const char *s);
void    perror(const char *s);

#endif
