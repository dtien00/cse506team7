#ifndef _MATH_H
#define _MATH_H

/* Stub math.h — rv64imac has no F/D extensions; floating-point functions are
   not implemented.  Types, classification macros, and constants are provided
   so that POSIX-compliant headers and code can be compiled. */

/* ── Types ─────────────────────────────────────────────────────────────────── */
/* FLT_EVAL_METHOD == 0 on RISC-V soft-float: float_t==float, double_t==double */
typedef float  float_t;
typedef double double_t;

/* ── FP classification kind constants ──────────────────────────────────────── */
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

/* ── Overflow / special-value macros ───────────────────────────────────────── */
#define HUGE_VAL    __builtin_inf()
#define HUGE_VALF   __builtin_inff()
#define HUGE_VALL   __builtin_infl()
#define INFINITY    __builtin_inff()
#define NAN         __builtin_nanf("")

/* ── Error-handling mode ───────────────────────────────────────────────────── */
#define MATH_ERRNO      1
#define MATH_ERREXCEPT  2
#define math_errhandling MATH_ERRNO

/* ── ilogb() sentinel values ───────────────────────────────────────────────── */
#define FP_ILOGB0    (-2147483647)
#define FP_ILOGBNAN   2147483647

/* ── Type-generic classification macros (GCC builtins, active under -fno-builtin) */
#define fpclassify(x) \
    __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, (x))
#define isfinite(x)          __builtin_isfinite(x)
#define isinf(x)             __builtin_isinf(x)
#define isnan(x)             __builtin_isnan(x)
#define isnormal(x)          __builtin_isnormal(x)
#define signbit(x)           __builtin_signbit(x)
#define isgreater(x,y)       __builtin_isgreater((x),(y))
#define isgreaterequal(x,y)  __builtin_isgreaterequal((x),(y))
#define isless(x,y)          __builtin_isless((x),(y))
#define islessequal(x,y)     __builtin_islessequal((x),(y))
#define islessgreater(x,y)   __builtin_islessgreater((x),(y))
#define isunordered(x,y)     __builtin_isunordered((x),(y))

/* ── Function declarations ──────────────────────────────────────────────────── */
double sqrt(double x);
double fabs(double x);

#endif /* _MATH_H */
