#ifndef _STDIO_H
#define _STDIO_H

#include "stddef.h"        /* NULL, size_t                      */
#include "stdarg.h"        /* va_list                           */
#include "sys/types.h"     /* off_t, ssize_t                    */

/* File-position type — complete object type, large enough for any offset */
typedef long fpos_t;

#define EOF         (-1)
#define BUFSIZ      256

/* Buffering modes for setvbuf() */
#define _IOFBF      0       /* fully buffered   */
#define _IOLBF      1       /* line buffered    */
#define _IONBF      2       /* unbuffered       */

/* File seek origins */
#ifndef SEEK_SET
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2
#endif

/* Implementation limits */
#define FILENAME_MAX    256     /* max pathname length this implementation handles */
#define FOPEN_MAX       16      /* minimum simultaneous open streams (POSIX >= 8)  */

/* [CX] minimum buffer size for ctermid() output */
#define L_ctermid       9       /* enough for "/dev/tty" + NUL */

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
#define stdin  stdin
#define stdout stdout
#define stderr stderr

/* ── Base POSIX functions ─────────────────────────────────────────────────── */

void    clearerr(FILE *);
int     fclose(FILE *);
int     feof(FILE *);
int     ferror(FILE *);
int     fflush(FILE *);
int     fgetc(FILE *);
int     fgetpos(FILE *restrict, fpos_t *restrict);
char   *fgets(char *restrict, int, FILE *restrict);
FILE   *fopen(const char *restrict, const char *restrict);
int     fprintf(FILE *restrict, const char *restrict, ...);
int     fputc(int, FILE *);
int     fputs(const char *restrict, FILE *restrict);
size_t  fread(void *restrict, size_t, size_t, FILE *restrict);
FILE   *freopen(const char *restrict, const char *restrict, FILE *restrict);
int     fscanf(FILE *restrict, const char *restrict, ...);
int     fseek(FILE *, long, int);
int     fsetpos(FILE *, const fpos_t *);
long    ftell(FILE *);
size_t  fwrite(const void *restrict, size_t, size_t, FILE *restrict);
int     getc(FILE *);
int     getchar(void);
void    perror(const char *);
int     printf(const char *restrict, ...);
int     putc(int, FILE *);
int     putchar(int);
int     puts(const char *);
int     remove(const char *);
int     rename(const char *, const char *);
void    rewind(FILE *);
int     scanf(const char *restrict, ...);
void    setbuf(FILE *restrict, char *restrict);
int     setvbuf(FILE *restrict, char *restrict, int, size_t);
int     snprintf(char *restrict, size_t, const char *restrict, ...);
int     sprintf(char *restrict, const char *restrict, ...);
int     sscanf(const char *restrict, const char *restrict, ...);
FILE   *tmpfile(void);
int     ungetc(int, FILE *);
int     vfprintf(FILE *restrict, const char *restrict, va_list);
int     vfscanf(FILE *restrict, const char *restrict, va_list);
int     vprintf(const char *restrict, va_list);
int     vscanf(const char *restrict, va_list);
int     vsnprintf(char *restrict, size_t, const char *restrict, va_list);
int     vsprintf(char *restrict, const char *restrict, va_list);
int     vsscanf(const char *restrict, const char *restrict, va_list);

/* ── [CX] POSIX extensions ────────────────────────────────────────────────── */

int     asprintf(char **, const char *restrict, ...);
int     vasprintf(char **, const char *restrict, va_list);
char   *ctermid(char *);
int     dprintf(int, const char *restrict, ...);
FILE   *fdopen(int, const char *);
int     fileno(FILE *);
void    flockfile(FILE *);
FILE   *fmemopen(void *restrict, size_t, const char *restrict);
int     fseeko(FILE *, off_t, int);
off_t   ftello(FILE *);
int     ftrylockfile(FILE *);
void    funlockfile(FILE *);
int     getc_unlocked(FILE *);
int     getchar_unlocked(void);
ssize_t getdelim(char **restrict, size_t *restrict, int, FILE *restrict);
ssize_t getline(char **restrict, size_t *restrict, FILE *restrict);
FILE   *open_memstream(char **, size_t *);
int     pclose(FILE *);
FILE   *popen(const char *, const char *);
int     putc_unlocked(int, FILE *);
int     putchar_unlocked(int);
int     renameat(int, const char *, int, const char *);
int     vdprintf(int, const char *restrict, va_list);

#endif /* _STDIO_H */
