#pragma once

// Fixed-width integer types for kernel
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
typedef int64_t   ssize_t;
typedef int32_t   pid_t;

#define NULL ((void *)0)
// typedef enum { false, true } bool; // apparently is now standard

#define USER_BASE 0x00000000UL
#define USER_TOP  0x80000000UL