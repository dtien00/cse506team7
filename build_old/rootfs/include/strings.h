#ifndef _STRINGS_H
#define _STRINGS_H

#include "stddef.h"    /* size_t */
#include "locale.h"    /* locale_t */

int strcasecmp(const char *, const char *);
int strcasecmp_l(const char *, const char *, locale_t);
int strncasecmp(const char *, const char *, size_t);
int strncasecmp_l(const char *, const char *, size_t, locale_t);

#endif /* _STRINGS_H */
