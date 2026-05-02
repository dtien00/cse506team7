#ifndef _STDDEF_H
#define _STDDEF_H

#include "types.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define offsetof(type, member) __builtin_offsetof(type, member)

typedef long ptrdiff_t;

#endif
