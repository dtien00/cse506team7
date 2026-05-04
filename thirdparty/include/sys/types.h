#pragma once
#include_next <sys/types.h>

#ifndef HAVE_CLOCK_T
typedef unsigned long clock_t;
#endif
#ifndef HAVE_USECONDS_T
typedef unsigned long useconds_t;
#endif
#ifndef HAVE_SUSECONDS_T
typedef long suseconds_t;
#endif
