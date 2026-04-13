#ifndef _MATH_H
#define _MATH_H

/* Stub math.h for MicroPython built with MICROPY_FLOAT_IMPL_NONE.
   No floating-point functions are needed; the header just must exist. */

#define INFINITY    __builtin_inff()
#define NAN         __builtin_nanf("")
#define HUGE_VAL    __builtin_inf()
#define HUGE_VALF   __builtin_inff()

#endif
