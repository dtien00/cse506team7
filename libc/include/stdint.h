#ifndef _STDINT_H
#define _STDINT_H

#include "types.h"

/* ── Exact-width types (defined in types.h) ─────────────────────────────── */
/* int8_t, int16_t, int32_t, int64_t                                         */
/* uint8_t, uint16_t, uint32_t, uint64_t                                     */
/* intmax_t, uintmax_t, intptr_t, uintptr_t                                  */

/* ── Minimum-width types ─────────────────────────────────────────────────── */
typedef int8_t           int_least8_t;
typedef int16_t          int_least16_t;
typedef int32_t          int_least32_t;
typedef int64_t          int_least64_t;

typedef uint8_t          uint_least8_t;
typedef uint16_t         uint_least16_t;
typedef uint32_t         uint_least32_t;
typedef uint64_t         uint_least64_t;

/* ── Fastest minimum-width types (RISC-V LP64: long is the native word) ─── */
typedef long             int_fast8_t;
typedef long             int_fast16_t;
typedef long             int_fast32_t;
typedef long             int_fast64_t;

typedef unsigned long    uint_fast8_t;
typedef unsigned long    uint_fast16_t;
typedef unsigned long    uint_fast32_t;
typedef unsigned long    uint_fast64_t;

/* ── Exact-width limits ──────────────────────────────────────────────────── */
#define INT8_MIN         (-128)
#define INT8_MAX         127
#define UINT8_MAX        0xffU

#define INT16_MIN        (-32767-1)
#define INT16_MAX        32767
#define UINT16_MAX       0xffffU

#define INT32_MIN        (-2147483647-1)
#define INT32_MAX        2147483647
#define UINT32_MAX       0xffffffffU

#define INT64_MIN        (-9223372036854775807LL-1)
#define INT64_MAX        9223372036854775807LL
#define UINT64_MAX       0xffffffffffffffffULL

/* ── Minimum-width limits ────────────────────────────────────────────────── */
#define INT_LEAST8_MIN   INT8_MIN
#define INT_LEAST8_MAX   INT8_MAX
#define UINT_LEAST8_MAX  UINT8_MAX

#define INT_LEAST16_MIN  INT16_MIN
#define INT_LEAST16_MAX  INT16_MAX
#define UINT_LEAST16_MAX UINT16_MAX

#define INT_LEAST32_MIN  INT32_MIN
#define INT_LEAST32_MAX  INT32_MAX
#define UINT_LEAST32_MAX UINT32_MAX

#define INT_LEAST64_MIN  INT64_MIN
#define INT_LEAST64_MAX  INT64_MAX
#define UINT_LEAST64_MAX UINT64_MAX

/* ── Fastest minimum-width limits (long = 64-bit on RISC-V LP64) ─────────── */
#define INT_FAST8_MIN    INT64_MIN
#define INT_FAST8_MAX    INT64_MAX
#define UINT_FAST8_MAX   UINT64_MAX

#define INT_FAST16_MIN   INT64_MIN
#define INT_FAST16_MAX   INT64_MAX
#define UINT_FAST16_MAX  UINT64_MAX

#define INT_FAST32_MIN   INT64_MIN
#define INT_FAST32_MAX   INT64_MAX
#define UINT_FAST32_MAX  UINT64_MAX

#define INT_FAST64_MIN   INT64_MIN
#define INT_FAST64_MAX   INT64_MAX
#define UINT_FAST64_MAX  UINT64_MAX

/* ── Pointer-type limits ─────────────────────────────────────────────────── */
#define INTPTR_MIN       INT64_MIN
#define INTPTR_MAX       INT64_MAX
#define UINTPTR_MAX      UINT64_MAX

/* ── Greatest-width limits ───────────────────────────────────────────────── */
#define INTMAX_MIN       INT64_MIN
#define INTMAX_MAX       INT64_MAX
#define UINTMAX_MAX      UINT64_MAX

/* ── Other required limits ───────────────────────────────────────────────── */
#define SIZE_MAX         UINT64_MAX
#define SSIZE_MAX        INT64_MAX

/* ptrdiff_t is signed long on LP64; its limits match intptr */
#define PTRDIFF_MIN      INT64_MIN
#define PTRDIFF_MAX      INT64_MAX

/* sig_atomic_t is typically int */
#define SIG_ATOMIC_MIN   INT32_MIN
#define SIG_ATOMIC_MAX   INT32_MAX

/* ── Constant expression macros ──────────────────────────────────────────── */
#define INT8_C(v)        (v)
#define INT16_C(v)       (v)
#define INT32_C(v)       (v)
#define INT64_C(v)       (v ## LL)

#define UINT8_C(v)       (v ## U)
#define UINT16_C(v)      (v ## U)
#define UINT32_C(v)      (v ## U)
#define UINT64_C(v)      (v ## ULL)

#define INTMAX_C(v)      (v ## LL)
#define UINTMAX_C(v)     (v ## ULL)

#endif /* _STDINT_H */
