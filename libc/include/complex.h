#ifndef _COMPLEX_H
#define _COMPLEX_H

/*
 * complex.h — POSIX/C11 complex number support.
 *
 * rv64imac has no hardware FP; complex arithmetic uses GCC's software
 * _Complex support.  Functions are declared here; implementations come
 * from libgcc's soft-float complex routines (linked via thirdparty builds)
 * or remain as weak stubs for code paths that never call them at runtime.
 */

/* C11 convenience macros */
#define complex      _Complex
#define _Complex_I   ((__extension__ 1.0iF))
#define I            _Complex_I

/* ── Function declarations ──────────────────────────────────────────────── */

/* Double-precision complex */
double _Complex cacos(double _Complex);
double _Complex casin(double _Complex);
double _Complex catan(double _Complex);
double _Complex ccos(double _Complex);
double _Complex csin(double _Complex);
double _Complex ctan(double _Complex);
double _Complex cacosh(double _Complex);
double _Complex casinh(double _Complex);
double _Complex catanh(double _Complex);
double _Complex ccosh(double _Complex);
double _Complex csinh(double _Complex);
double _Complex ctanh(double _Complex);
double _Complex cexp(double _Complex);
double _Complex clog(double _Complex);
double _Complex cpow(double _Complex, double _Complex);
double _Complex csqrt(double _Complex);
double             cabs(double _Complex);
double             carg(double _Complex);
double             cimag(double _Complex);
double _Complex conj(double _Complex);
double _Complex cproj(double _Complex);
double             creal(double _Complex);

/* Float-precision complex */
float _Complex cacosf(float _Complex);
float _Complex casinf(float _Complex);
float _Complex catanf(float _Complex);
float _Complex ccosf(float _Complex);
float _Complex csinf(float _Complex);
float _Complex ctanf(float _Complex);
float _Complex cacoshf(float _Complex);
float _Complex casinhf(float _Complex);
float _Complex catanhf(float _Complex);
float _Complex ccoshf(float _Complex);
float _Complex csinhf(float _Complex);
float _Complex ctanhf(float _Complex);
float _Complex cexpf(float _Complex);
float _Complex clogf(float _Complex);
float _Complex cpowf(float _Complex, float _Complex);
float _Complex csqrtf(float _Complex);
float              cabsf(float _Complex);
float              cargf(float _Complex);
float              cimagf(float _Complex);
float _Complex conjf(float _Complex);
float _Complex cprojf(float _Complex);
float              crealf(float _Complex);

/* Long-double-precision complex */
long double _Complex cacosl(long double _Complex);
long double _Complex casinl(long double _Complex);
long double _Complex catanl(long double _Complex);
long double _Complex ccosl(long double _Complex);
long double _Complex csinl(long double _Complex);
long double _Complex ctanl(long double _Complex);
long double _Complex cacoshl(long double _Complex);
long double _Complex casinhl(long double _Complex);
long double _Complex catanhl(long double _Complex);
long double _Complex ccoshl(long double _Complex);
long double _Complex csinhl(long double _Complex);
long double _Complex ctanhl(long double _Complex);
long double _Complex cexpl(long double _Complex);
long double _Complex clogl(long double _Complex);
long double _Complex cpowl(long double _Complex, long double _Complex);
long double _Complex csqrtl(long double _Complex);
long double             cabsl(long double _Complex);
long double             cargl(long double _Complex);
long double             cimagl(long double _Complex);
long double _Complex conjl(long double _Complex);
long double _Complex cprojl(long double _Complex);
long double             creall(long double _Complex);

#endif /* _COMPLEX_H */
