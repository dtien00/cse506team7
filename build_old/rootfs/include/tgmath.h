#ifndef _TGMATH_H
#define _TGMATH_H

/*
 * tgmath.h — POSIX/C11 type-generic math macros.
 *
 * Each macro dispatches to the correct f/d/l (and complex) variant based on
 * the types of its generic parameters using C11 _Generic.
 *
 * Dispatch rules (POSIX.1-2024 / ISO C11 §7.25):
 *   1. If any generic-parameter argument has type long double [complex],
 *      the long double variant is selected.
 *   2. Else if any argument has type double, double complex, or integer type,
 *      the double variant is selected.
 *   3. Else (all arguments are float / float complex), the float variant.
 *   4. If at least one argument is complex, the complex variant is chosen.
 *
 * Implementation note: (name)(args) — parenthesising the function name
 * suppresses recursive macro expansion while still calling the function.
 */

#include "math.h"
#ifndef __TINYC__
#include "complex.h"
#endif

/* ── Internal dispatch helpers ──────────────────────────────────────────── */

#ifndef __TINYC__
#define _TG_CPLX_REAL1(name, x) \
    _Generic((x), \
        long double _Complex: (c##name##l)(x), \
        float _Complex:       (c##name##f)(x), \
        double _Complex:      (c##name)  (x), \
        long double:          (name##l)  (x), \
        float:                (name##f)  (x), \
        default:              (name)     (x)  \
    )

#define _TG_CPLX_REAL2(name, x, y) \
    _Generic((x)+(y), \
        long double _Complex: (c##name##l)((x),(y)), \
        float _Complex:       (c##name##f)((x),(y)), \
        double _Complex:      (c##name)  ((x),(y)), \
        long double:          (name##l)  ((x),(y)), \
        float:                (name##f)  ((x),(y)), \
        default:              (name)     ((x),(y))  \
    )
#else
/* TCC does not support _Complex — fall back to real-only dispatch */
#define _TG_CPLX_REAL1(name, x) \
    _Generic((x), \
        long double: (name##l)(x), \
        float:       (name##f)(x), \
        default:     (name)   (x)  \
    )
#define _TG_CPLX_REAL2(name, x, y) \
    _Generic((x)+(y), \
        long double: (name##l)((x),(y)), \
        float:       (name##f)((x),(y)), \
        default:     (name)   ((x),(y))  \
    )
#endif

/* Real-only, 1 generic parameter */
#define _TG_REAL1(name, x) \
    _Generic((x), \
        long double: (name##l)(x), \
        float:       (name##f)(x), \
        default:     (name)   (x)  \
    )

/* Real-only, 2 generic parameters */
#define _TG_REAL2(name, x, y) \
    _Generic((x)+(y), \
        long double: (name##l)((x),(y)), \
        float:       (name##f)((x),(y)), \
        default:     (name)   ((x),(y))  \
    )

/* Real-only, 3 generic parameters */
#define _TG_REAL3(name, x, y, z) \
    _Generic((x)+(y)+(z), \
        long double: (name##l)((x),(y),(z)), \
        float:       (name##f)((x),(y),(z)), \
        default:     (name)   ((x),(y),(z))  \
    )

/*
 * _TG_REAL1_INT(name, x, iarg)
 *   Functions like frexp/ldexp where only x is the generic parameter but
 *   there is a non-generic integer/pointer second argument.
 */
#define _TG_REAL1_INT(name, x, iarg) \
    _Generic((x), \
        long double: (name##l)((x),(iarg)), \
        float:       (name##f)((x),(iarg)), \
        default:     (name)   ((x),(iarg))  \
    )

/* ── Functions with both real and complex variants ─────────────────────── */

#undef acos
#define acos(x)   _TG_CPLX_REAL1(acos,  x)
#undef asin
#define asin(x)   _TG_CPLX_REAL1(asin,  x)
#undef atan
#define atan(x)   _TG_CPLX_REAL1(atan,  x)
#undef acosh
#define acosh(x)  _TG_CPLX_REAL1(acosh, x)
#undef asinh
#define asinh(x)  _TG_CPLX_REAL1(asinh, x)
#undef atanh
#define atanh(x)  _TG_CPLX_REAL1(atanh, x)
#undef cos
#define cos(x)    _TG_CPLX_REAL1(cos,   x)
#undef sin
#define sin(x)    _TG_CPLX_REAL1(sin,   x)
#undef tan
#define tan(x)    _TG_CPLX_REAL1(tan,   x)
#undef cosh
#define cosh(x)   _TG_CPLX_REAL1(cosh,  x)
#undef sinh
#define sinh(x)   _TG_CPLX_REAL1(sinh,  x)
#undef tanh
#define tanh(x)   _TG_CPLX_REAL1(tanh,  x)
#undef exp
#define exp(x)    _TG_CPLX_REAL1(exp,   x)
#undef log
#define log(x)    _TG_CPLX_REAL1(log,   x)
#undef sqrt
#define sqrt(x)   _TG_CPLX_REAL1(sqrt,  x)

#undef pow
#define pow(x,y)  _TG_CPLX_REAL2(pow,   x, y)

/* fabs / cabs: complex version has a different name pattern */
#undef fabs
#ifndef __TINYC__
#define fabs(x) \
    _Generic((x), \
        long double _Complex: (cabsl)(x), \
        float _Complex:       (cabsf)(x), \
        double _Complex:      (cabs) (x), \
        long double:          (fabsl)(x), \
        float:                (fabsf)(x), \
        default:              (fabs) (x)  \
    )
#else
#define fabs(x) \
    _Generic((x), \
        long double: (fabsl)(x), \
        float:       (fabsf)(x), \
        default:     (fabs) (x)  \
    )
#endif

/* ── Real-only functions ────────────────────────────────────────────────── */

#undef atan2
#define atan2(y,x)       _TG_REAL2(atan2,      y, x)
#undef cbrt
#define cbrt(x)          _TG_REAL1(cbrt,       x)
#undef ceil
#define ceil(x)          _TG_REAL1(ceil,       x)
#undef copysign
#define copysign(x,y)    _TG_REAL2(copysign,   x, y)
#undef erf
#define erf(x)           _TG_REAL1(erf,        x)
#undef erfc
#define erfc(x)          _TG_REAL1(erfc,       x)
#undef exp2
#define exp2(x)          _TG_REAL1(exp2,       x)
#undef expm1
#define expm1(x)         _TG_REAL1(expm1,      x)
#undef fdim
#define fdim(x,y)        _TG_REAL2(fdim,       x, y)
#undef floor
#define floor(x)         _TG_REAL1(floor,      x)
#undef fma
#define fma(x,y,z)       _TG_REAL3(fma,        x, y, z)
#undef fmax
#define fmax(x,y)        _TG_REAL2(fmax,       x, y)
#undef fmin
#define fmin(x,y)        _TG_REAL2(fmin,       x, y)
#undef fmod
#define fmod(x,y)        _TG_REAL2(fmod,       x, y)
#undef hypot
#define hypot(x,y)       _TG_REAL2(hypot,      x, y)
#undef ilogb
#define ilogb(x)         _TG_REAL1(ilogb,      x)
#undef lgamma
#define lgamma(x)        _TG_REAL1(lgamma,     x)
#undef llrint
#define llrint(x)        _TG_REAL1(llrint,     x)
#undef llround
#define llround(x)       _TG_REAL1(llround,    x)
#undef log10
#define log10(x)         _TG_REAL1(log10,      x)
#undef log1p
#define log1p(x)         _TG_REAL1(log1p,      x)
#undef log2
#define log2(x)          _TG_REAL1(log2,       x)
#undef logb
#define logb(x)          _TG_REAL1(logb,       x)
#undef lrint
#define lrint(x)         _TG_REAL1(lrint,      x)
#undef lround
#define lround(x)        _TG_REAL1(lround,     x)
#undef nearbyint
#define nearbyint(x)     _TG_REAL1(nearbyint,  x)
#undef nextafter
#define nextafter(x,y)   _TG_REAL2(nextafter,  x, y)
#undef nexttoward
#define nexttoward(x,y) \
    _Generic((x), \
        long double: (nexttowardl)((x),(y)), \
        float:       (nexttowardf)((x),(y)), \
        default:     (nexttoward) ((x),(y))  \
    )
#undef remainder
#define remainder(x,y)   _TG_REAL2(remainder,  x, y)
#undef rint
#define rint(x)          _TG_REAL1(rint,       x)
#undef round
#define round(x)         _TG_REAL1(round,      x)
#undef scalbln
#define scalbln(x,n)     _TG_REAL1_INT(scalbln, x, n)
#undef scalbn
#define scalbn(x,n)      _TG_REAL1_INT(scalbn,  x, n)
#undef tgamma
#define tgamma(x)        _TG_REAL1(tgamma,     x)
#undef trunc
#define trunc(x)         _TG_REAL1(trunc,      x)

/* remquo: generic on first two args; third is a non-generic int* */
#undef remquo
#define remquo(x,y,q) \
    _Generic((x)+(y), \
        long double: (remquol)((x),(y),(q)), \
        float:       (remquof)((x),(y),(q)), \
        default:     (remquo) ((x),(y),(q))  \
    )

/* frexp: generic on x; second arg is non-generic int* */
#undef frexp
#define frexp(x,e)  _TG_REAL1_INT(frexp, x, e)

/* ldexp: generic on x; second arg is non-generic int */
#undef ldexp
#define ldexp(x,e)  _TG_REAL1_INT(ldexp, x, e)

/* ── Complex-only functions (no real counterpart) ───────────────────────── */
#ifndef __TINYC__

#undef carg
#define carg(z) \
    _Generic((z), \
        long double _Complex: (cargl)(z), \
        float _Complex:       (cargf)(z), \
        default:              (carg) (z)  \
    )

#undef cimag
#define cimag(z) \
    _Generic((z), \
        long double _Complex: (cimagl)(z), \
        float _Complex:       (cimagf)(z), \
        default:              (cimag) (z)  \
    )

#undef conj
#define conj(z) \
    _Generic((z), \
        long double _Complex: (conjl)(z), \
        float _Complex:       (conjf)(z), \
        default:              (conj) (z)  \
    )

#undef cproj
#define cproj(z) \
    _Generic((z), \
        long double _Complex: (cprojl)(z), \
        float _Complex:       (cprojf)(z), \
        default:              (cproj) (z)  \
    )

#undef creal
#define creal(z) \
    _Generic((z), \
        long double _Complex: (creall)(z), \
        float _Complex:       (crealf)(z), \
        default:              (creal) (z)  \
    )

#endif /* !__TINYC__ */

#endif /* _TGMATH_H */
