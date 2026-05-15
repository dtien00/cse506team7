#ifndef _STRING_H
#define _STRING_H

#include "types.h"
#include "locale.h"

/* Memory functions — base POSIX */
void  *memchr(const void *s, int c, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);
void  *memcpy(void *restrict dest, const void *restrict src, size_t n);
void  *memmove(void *dest, const void *src, size_t n);
void  *memset(void *s, int c, size_t n);

/* Memory functions — CX */
void  *memmem(const void *haystack, size_t haystacklen,
              const void *needle, size_t needlelen);

/* String copy/concatenation — base POSIX */
char  *strcat(char *restrict dest, const char *restrict src);
char  *strcpy(char *restrict dest, const char *restrict src);
char  *strncat(char *restrict dest, const char *restrict src, size_t n);
char  *strncpy(char *restrict dest, const char *restrict src, size_t n);

/* String copy/concatenation — CX */
char  *stpcpy(char *restrict dest, const char *restrict src);
char  *stpncpy(char *restrict dest, const char *restrict src, size_t n);
size_t strlcat(char *restrict dest, const char *restrict src, size_t size);
size_t strlcpy(char *restrict dest, const char *restrict src, size_t size);

/* String comparison — base POSIX */
int    strcmp(const char *s1, const char *s2);
int    strcoll(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
size_t strxfrm(char *restrict dest, const char *restrict src, size_t n);

/* String comparison — CX (locale-aware) */
int    strcoll_l(const char *s1, const char *s2, locale_t locale);
size_t strxfrm_l(char *restrict dest, const char *restrict src,
                 size_t n, locale_t locale);

/* String search — base POSIX */
char  *strchr(const char *s, int c);
size_t strcspn(const char *s, const char *reject);
char  *strpbrk(const char *s, const char *accept);
char  *strrchr(const char *s, int c);
size_t strspn(const char *s, const char *accept);
char  *strstr(const char *haystack, const char *needle);
char  *strtok(char *restrict str, const char *restrict delim);

/* String search — CX */
char  *strtok_r(char *restrict str, const char *restrict delim,
                char **restrict saveptr);

/* String length — base POSIX */
size_t strlen(const char *s);

/* String length — CX */
size_t strnlen(const char *s, size_t maxlen);

/* String duplication — CX */
char  *strdup(const char *s);
char  *strndup(const char *s, size_t n);

/* Error/signal strings — base POSIX */
char  *strerror(int errnum);

/* Error/signal strings — CX */
int    strerror_r(int errnum, char *buf, size_t buflen);
char  *strerror_l(int errnum, locale_t locale);
char  *strsignal(int signum);

#endif /* _STRING_H */
