#ifndef _STDINT_H
#define _STDINT_H

#include "types.h"

#define INT8_MIN   (-128)
#define INT16_MIN  (-32767-1)
#define INT32_MIN  (-2147483647-1)
#define INT64_MIN  (-9223372036854775807LL-1)

#define INT8_MAX   127
#define INT16_MAX  32767
#define INT32_MAX  2147483647
#define INT64_MAX  9223372036854775807LL

#define UINT8_MAX  0xff
#define UINT16_MAX 0xffff
#define UINT32_MAX 0xffffffffU
#define UINT64_MAX 0xffffffffffffffffULL

#define INTPTR_MIN  INT64_MIN
#define INTPTR_MAX  INT64_MAX
#define UINTPTR_MAX UINT64_MAX

#define SIZE_MAX UINT64_MAX

#endif
