#ifndef _STDLIB_H
#define _STDLIB_H

#include "stddef.h"    /* size_t, wchar_t, NULL */
#include "fcntl.h"     /* O_APPEND, O_CLOEXEC, O_CLOFORK, O_SYNC [CX] */
#include "sys/wait.h"  /* WCOREDUMP, WEXITSTATUS, WIFEXITED, WIFSIGNALED,
                          WIFSTOPPED, WNOHANG, WSTOPSIG, WTERMSIG, WUNTRACED [CX] */

/* --- Termination --- */
#define EXIT_SUCCESS  0
#define EXIT_FAILURE  1
#define RAND_MAX      2147483647

/* MB_CUR_MAX: max bytes per multibyte char in current locale (CX).
 * In the POSIX locale this is always 1. */
#define MB_CUR_MAX    ((size_t)1)

__attribute__((noreturn)) void exit(int status);
__attribute__((noreturn)) void _Exit(int status);
__attribute__((noreturn)) void abort(void);
__attribute__((noreturn)) void quick_exit(int status);

int  atexit(void (*func)(void));
int  at_quick_exit(void (*func)(void));

/* --- Memory allocation --- */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void *reallocarray(void *ptr, size_t nmemb, size_t size);  /* CX */
void *aligned_alloc(size_t alignment, size_t size);
void  free(void *ptr);

/* --- Temporary files / paths (CX) --- */
char *mkdtemp(char *tmpl);
int   mkstemp(char *tmpl);
int   mkostemp(char *tmpl, int flags);
char *realpath(const char *restrict path, char *restrict resolved);

/* --- Environment --- */
char *getenv(const char *name);
char *secure_getenv(const char *name);  /* CX */
int   setenv(const char *name, const char *value, int overwrite);  /* CX */
int   unsetenv(const char *name);  /* CX */
int   system(const char *command);

/* --- String-to-number conversion --- */
int                atoi(const char *nptr);
long               atol(const char *nptr);
long long          atoll(const char *nptr);
double             atof(const char *nptr);

char* getenv(const char* name);
int putenv(char* string);
int setenv(const char* name, const char* value, int overwrite);
int unsetenv(const char* name);
double             strtod(const char *restrict nptr, char **restrict endptr);
float              strtof(const char *restrict nptr, char **restrict endptr);
long double        strtold(const char *restrict nptr, char **restrict endptr);
long               strtol(const char *restrict nptr, char **restrict endptr, int base);
unsigned long      strtoul(const char *restrict nptr, char **restrict endptr, int base);
long long          strtoll(const char *restrict nptr, char **restrict endptr, int base);
unsigned long long strtoull(const char *restrict nptr, char **restrict endptr, int base);

/* --- Multibyte / wide-character conversion --- */
int    mblen(const char *s, size_t n);
int    mbtowc(wchar_t *restrict pwc, const char *restrict s, size_t n);
int    wctomb(char *s, wchar_t wc);
size_t mbstowcs(wchar_t *restrict pwcs, const char *restrict s, size_t n);
size_t wcstombs(char *restrict s, const wchar_t *restrict pwcs, size_t n);

/* --- Random --- */
int  rand(void);
void srand(unsigned int seed);

/* --- Integer arithmetic --- */
int       abs(int j);
long      labs(long j);
long long llabs(long long j);

typedef struct { int       quot; int       rem; } div_t;
typedef struct { long      quot; long      rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

div_t   div(int numer, int denom);
ldiv_t  ldiv(long numer, long denom);
lldiv_t lldiv(long long numer, long long denom);

/* --- Search / sort --- */
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
void  qsort(void *base, size_t nmemb, size_t size,
            int (*compar)(const void *, const void *));
void  qsort_r(void *base, size_t nmemb, size_t size,           /* CX */
              int (*compar)(const void *, const void *, void *),
              void *arg);

/* --- Miscellaneous (CX) --- */
int getsubopt(char **restrict optionp, char *const *restrict tokens,
              char **restrict valuep);

#endif /* _STDLIB_H */
