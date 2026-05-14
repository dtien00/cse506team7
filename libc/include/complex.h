#ifndef _COMPLEX_H
#define _COMPLEX_H

/*
 * complex.h — POSIX.1-2024 / ISO C11 complex arithmetic.
 *
 * Macros and function prototypes follow §7.3 of the C11 standard as required
 * by POSIX.1-2024.
 */

/* ── Required macros (POSIX §B.11 / C11 §7.3.1) ─────────────────────────── */

/* complex expands to _Complex (C11 §7.3.1p2) */
#define complex         _Complex

/* _Complex_I: constant expression of type const float _Complex with value i */
#define _Complex_I      (__extension__ (const float _Complex){ 0.0f, 1.0f })

/* imaginary / _Imaginary_I — defined when the implementation supports
 * imaginary types.  GCC supports _Imaginary via the GNU extension
 * 1.0iF (float imaginary literal), which is a proper constant expression. */
#ifdef __GNUC__
#  define imaginary       _Imaginary
#  define _Imaginary_I    (__extension__ 1.0iF)
/* I expands to _Imaginary_I when imaginary types are supported (C11 §7.3.1p4) */
#  define I               _Imaginary_I
#else
#  define I               _Complex_I
#endif

/* ── Construction macros (POSIX requires these be defined as macros) ─────── */
/* __builtin_complex(re, im) creates a complex from two real components and
 * is a constant expression, satisfying C11 §7.3.1 requirements.            */
#define CMPLX(x, y)   __builtin_complex((double)(x),      (double)(y))
#define CMPLXF(x, y)  __builtin_complex((float)(x),       (float)(y))
#define CMPLXL(x, y)  __builtin_complex((long double)(x), (long double)(y))

/* ── Function prototypes ────────────────────────────────────────────────── */
/* Using "double complex" etc. (which expand to "double _Complex") to match
 * the POSIX prototype spelling exactly.                                     */

double               cabs(double complex);
float                cabsf(float complex);
long double          cabsl(long double complex);

double complex       cacos(double complex);
float complex        cacosf(float complex);
long double complex  cacosl(long double complex);

double complex       cacosh(double complex);
float complex        cacoshf(float complex);
long double complex  cacoshl(long double complex);

double               carg(double complex);
float                cargf(float complex);
long double          cargl(long double complex);

double complex       casin(double complex);
float complex        casinf(float complex);
long double complex  casinl(long double complex);

double complex       casinh(double complex);
float complex        casinhf(float complex);
long double complex  casinhl(long double complex);

double complex       catan(double complex);
float complex        catanf(float complex);
long double complex  catanl(long double complex);

double complex       catanh(double complex);
float complex        catanhf(float complex);
long double complex  catanhl(long double complex);

double complex       ccos(double complex);
float complex        ccosf(float complex);
long double complex  ccosl(long double complex);

double complex       ccosh(double complex);
float complex        ccoshf(float complex);
long double complex  ccoshl(long double complex);

double complex       cexp(double complex);
float complex        cexpf(float complex);
long double complex  cexpl(long double complex);

double               cimag(double complex);
float                cimagf(float complex);
long double          cimagl(long double complex);

double complex       clog(double complex);
float complex        clogf(float complex);
long double complex  clogl(long double complex);

double complex       conj(double complex);
float complex        conjf(float complex);
long double complex  conjl(long double complex);

double complex       cpow(double complex, double complex);
float complex        cpowf(float complex, float complex);
long double complex  cpowl(long double complex, long double complex);

double complex       cproj(double complex);
float complex        cprojf(float complex);
long double complex  cprojl(long double complex);

double               creal(double complex);
float                crealf(float complex);
long double          creall(long double complex);

double complex       csin(double complex);
float complex        csinf(float complex);
long double complex  csinl(long double complex);

double complex       csinh(double complex);
float complex        csinhf(float complex);
long double complex  csinhl(long double complex);

double complex       csqrt(double complex);
float complex        csqrtf(float complex);
long double complex  csqrtl(long double complex);

double complex       ctan(double complex);
float complex        ctanf(float complex);
long double complex  ctanl(long double complex);

double complex       ctanh(double complex);
float complex        ctanhf(float complex);
long double complex  ctanhl(long double complex);

#endif /* _COMPLEX_H */
