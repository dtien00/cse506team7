#pragma once

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef long long          int64_t;
typedef int                int32_t;
typedef short              int16_t;
typedef signed char        int8_t;

typedef uint64_t  uintptr_t;   // RISC-V 64-bit
typedef int64_t   intptr_t;
typedef uint64_t  size_t;
typedef int64_t   ssize_t;
typedef int32_t   pid_t;
typedef int64_t   off_t;
typedef uint32_t  mode_t;

#define NULL ((void *)0)
// typedef enum { false, true } bool; // apparently is now standard
