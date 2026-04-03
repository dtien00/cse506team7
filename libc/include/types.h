#pragma once
#include <stdint.h>   // uint8_t, uint16_t, uint32_t, uint64_t, int8_t..int64_t, uintptr_t, intptr_t
#include <stddef.h>   // ptrdiff_t

#ifndef __SIZE_TYPE__ // somehow size_t is undefined
typedef unsigned long size_t;
#else
typedef __SIZE_TYPE__ size_t;
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

// POSIX types not provided by freestanding stdint/stddef
typedef int64_t   ssize_t;
typedef int32_t   pid_t;
typedef int64_t   off_t; // file offset (signed, supports seeking)
typedef uint32_t  mode_t;

typedef unsigned long  ino_t;   // inode number