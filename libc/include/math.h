#ifndef _MATH_H
#define _MATH_H

/* Stub math.h for MicroPython built with MICROPY_FLOAT_IMPL_NONE.
   No floating-point functions are needed; the header just must exist. */

/* Floating-point classification constants required by C11/POSIX */
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

/* Overflow / special-value macros */
#define INFINITY    __builtin_inff()
#define NAN         __builtin_nanf("")
#define HUGE_VAL    __builtin_inf()
#define HUGE_VALF   __builtin_inff()
#define HUGE_VALL   __builtin_infl()

#endif /* _MATH_H */
