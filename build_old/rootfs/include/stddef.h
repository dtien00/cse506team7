#ifndef _STDDEF_H
#define _STDDEF_H

#include "types.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

typedef long          ptrdiff_t;
typedef __WCHAR_TYPE__ wchar_t;
typedef union {
    long long   __max_align_ll;
    long double __max_align_ld;
} max_align_t;

#endif
