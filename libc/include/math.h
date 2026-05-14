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

/* ── M_* constants (XSI — double and long double variants) ─────────────────── */
/* Values accurate to at least 20 significant decimal digits.                   */
#define M_E            2.7182818284590452354   /* e                    */
#define M_El           2.7182818284590452354L
#define M_LOG2E        1.4426950408889634074   /* log2(e)              */
#define M_LOG2El       1.4426950408889634074L
#define M_LOG10E       0.43429448190325182765  /* log10(e)             */
#define M_LOG10El      0.43429448190325182765L
#define M_LN2          0.69314718055994530942  /* ln(2)                */
#define M_LN2l         0.69314718055994530942L
#define M_LN10         2.30258509299404568402  /* ln(10)               */
#define M_LN10l        2.30258509299404568402L
#define M_PI           3.14159265358979323846  /* π                    */
#define M_PIl          3.14159265358979323846L
#define M_PI_2         1.57079632679489661923  /* π/2                  */
#define M_PI_2l        1.57079632679489661923L
#define M_PI_4         0.78539816339744830962  /* π/4                  */
#define M_PI_4l        0.78539816339744830962L
#define M_1_PI         0.31830988618379067154  /* 1/π                  */
#define M_1_PIl        0.31830988618379067154L
#define M_2_PI         0.63661977236758134308  /* 2/π                  */
#define M_2_PIl        0.63661977236758134308L
#define M_1_SQRTPI     0.56418958354775628694  /* 1/√π  (POSIX.1-2024) */
#define M_1_SQRTPIl    0.56418958354775628694L
#define M_2_SQRTPI     1.12837916709551257390  /* 2/√π                 */
#define M_2_SQRTPIl    1.12837916709551257390L
#define M_SQRT2        1.41421356237309504880  /* √2                   */
#define M_SQRT2l       1.41421356237309504880L
#define M_SQRT1_2      0.70710678118654752440  /* 1/√2                 */
#define M_SQRT1_2l     0.70710678118654752440L
#define M_SQRT3        1.73205080756887729352  /* √3   (POSIX.1-2024)  */
#define M_SQRT3l       1.73205080756887729352L
#define M_SQRT1_3      0.57735026918962576450  /* 1/√3 (POSIX.1-2024)  */
#define M_SQRT1_3l     0.57735026918962576450L
#define M_EGAMMA       0.57721566490153286060  /* γ    (POSIX.1-2024)  */
#define M_EGAMMAl      0.57721566490153286060L
#define M_PHI          1.61803398874989484820  /* φ    (POSIX.1-2024)  */
#define M_PHIl         1.61803398874989484820L

/* ── XSI: signgam — sign of lgamma result ───────────────────────────────────── */
extern int signgam;

/* ── POSIX function declarations — double, float (f), long double (l) ──────── */

double      acos(double);
float       acosf(float);
long double acosl(long double);

double      asin(double);
float       asinf(float);
long double asinl(long double);

double      atan(double);
float       atanf(float);
long double atanl(long double);

double      atan2(double, double);
float       atan2f(float, float);
long double atan2l(long double, long double);

double      cos(double);
float       cosf(float);
long double cosl(long double);

double      sin(double);
float       sinf(float);
long double sinl(long double);

double      tan(double);
float       tanf(float);
long double tanl(long double);

double      acosh(double);
float       acoshf(float);
long double acoshl(long double);

double      asinh(double);
float       asinhf(float);
long double asinhl(long double);

double      atanh(double);
float       atanhf(float);
long double atanhl(long double);

double      cosh(double);
float       coshf(float);
long double coshl(long double);

double      sinh(double);
float       sinhf(float);
long double sinhl(long double);

double      tanh(double);
float       tanhf(float);
long double tanhl(long double);

double      exp(double);
float       expf(float);
long double expl(long double);

double      exp2(double);
float       exp2f(float);
long double exp2l(long double);

double      expm1(double);
float       expm1f(float);
long double expm1l(long double);

double      frexp(double, int *);
float       frexpf(float, int *);
long double frexpl(long double, int *);

int         ilogb(double);
int         ilogbf(float);
int         ilogbl(long double);

double      ldexp(double, int);
float       ldexpf(float, int);
long double ldexpl(long double, int);

double      log(double);
float       logf(float);
long double logl(long double);

double      log10(double);
float       log10f(float);
long double log10l(long double);

double      log1p(double);
float       log1pf(float);
long double log1pl(long double);

double      log2(double);
float       log2f(float);
long double log2l(long double);

double      logb(double);
float       logbf(float);
long double logbl(long double);

double      modf(double, double *);
float       modff(float, float *);
long double modfl(long double, long double *);

double      scalbn(double, int);
float       scalbnf(float, int);
long double scalbnl(long double, int);

double      scalbln(double, long);
float       scalblnf(float, long);
long double scalblnl(long double, long);

double      cbrt(double);
float       cbrtf(float);
long double cbrtl(long double);

double      fabs(double);
float       fabsf(float);
long double fabsl(long double);

double      hypot(double, double);
float       hypotf(float, float);
long double hypotl(long double, long double);

double      pow(double, double);
float       powf(float, float);
long double powl(long double, long double);

double      sqrt(double);
float       sqrtf(float);
long double sqrtl(long double);

double      erf(double);
float       erff(float);
long double erfl(long double);

double      erfc(double);
float       erfcf(float);
long double erfcl(long double);

double      lgamma(double);
float       lgammaf(float);
long double lgammal(long double);

double      tgamma(double);
float       tgammaf(float);
long double tgammal(long double);

double      ceil(double);
float       ceilf(float);
long double ceill(long double);

double      floor(double);
float       floorf(float);
long double floorl(long double);

double      nearbyint(double);
float       nearbyintf(float);
long double nearbyintl(long double);

double      rint(double);
float       rintf(float);
long double rintl(long double);

long        lrint(double);
long        lrintf(float);
long        lrintl(long double);

long long   llrint(double);
long long   llrintf(float);
long long   llrintl(long double);

double      round(double);
float       roundf(float);
long double roundl(long double);

long        lround(double);
long        lroundf(float);
long        lroundl(long double);

long long   llround(double);
long long   llroundf(float);
long long   llroundl(long double);

double      trunc(double);
float       truncf(float);
long double truncl(long double);

double      fmod(double, double);
float       fmodf(float, float);
long double fmodl(long double, long double);

double      remainder(double, double);
float       remainderf(float, float);
long double remainderl(long double, long double);

double      remquo(double, double, int *);
float       remquof(float, float, int *);
long double remquol(long double, long double, int *);

double      copysign(double, double);
float       copysignf(float, float);
long double copysignl(long double, long double);

double      nan(const char *);
float       nanf(const char *);
long double nanl(const char *);

double      nextafter(double, double);
float       nextafterf(float, float);
long double nextafterl(long double, long double);

double      nexttoward(double, long double);
float       nexttowardf(float, long double);
long double nexttowardl(long double, long double);

double      fdim(double, double);
float       fdimf(float, float);
long double fdiml(long double, long double);

double      fmax(double, double);
float       fmaxf(float, float);
long double fmaxl(long double, long double);

double      fmin(double, double);
float       fminf(float, float);
long double fminl(long double, long double);

double      fma(double, double, double);
float       fmaf(float, float, float);
long double fmal(long double, long double, long double);

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

/* ── XSI: Bessel / Neumann functions ────────────────────────────────────────── */
double j0(double);
double j1(double);
double jn(int, double);
double y0(double);
double y1(double);
double yn(int, double);

#endif /* _MATH_H */
