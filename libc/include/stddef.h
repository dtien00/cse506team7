#ifndef _STDDEF_H
#define _STDDEF_H

#include "types.h"

#define NULL ((void*)0)

#define offsetof(type, member) ((size_t)&((type*)0)->member)

typedef long ptrdiff_t;

#endif
