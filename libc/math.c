/*
 * math.c — libc implementations of POSIX math functions.
 *
 * rv64imac has no hardware FP; arithmetic uses GCC's soft-float support.
 * Trivial functions are implemented directly.  Transcendental functions
 * (sin, cos, exp, log, pow, sqrt, …) delegate to GCC __builtin_* forms,
 * which are lowered to libgcc soft-float routines at link time.
 *
 * All functions are provided as strong symbols so they override weak stubs
 * in thirdparty/compat.c.
 */

#include "math.h"
#include "string.h"

/* ── Helpers ────────────────────────────────────────────────────────────── */

/* Signed-bit extraction via type-punning through union (avoids UB). */
static unsigned int   _float_bits(float x)       { union {float f; unsigned i;} u; u.f=x; return u.i; }
static float          _bits_float(unsigned i)     { union {float f; unsigned i;} u; u.i=i; return u.f; }
static unsigned long long _double_bits(double x)  { union {double d; unsigned long long i;} u; u.d=x; return u.i; }
static double         _bits_double(unsigned long long i) { union {double d; unsigned long long i;} u; u.i=i; return u.d; }

/* ── fabs ────────────────────────────────────────────────────────────────── */
float       fabsf(float x)       { return _bits_float(_float_bits(x) & 0x7fffffffu); }
double      fabs(double x)       { return _bits_double(_double_bits(x) & 0x7fffffffffffffffull); }
long double fabsl(long double x) {
    /* 128-bit quad-precision: sign bit is MSB of the upper 8 bytes (little-endian). */
    unsigned long long p[2];
    memcpy(p, &x, 16);
    p[1] &= 0x7fffffffffffffffull;
    long double r; memcpy(&r, p, 16); return r;
}

/* ── copysign ────────────────────────────────────────────────────────────── */
float copysignf(float x, float y) {
    return _bits_float((_float_bits(x) & 0x7fffffffu) |
                       (_float_bits(y) & 0x80000000u));
}
double copysign(double x, double y) {
    return _bits_double((_double_bits(x) & 0x7fffffffffffffffull) |
                        (_double_bits(y) & 0x8000000000000000ull));
}
long double copysignl(long double x, long double y) {
    unsigned long long px[2], py[2];
    memcpy(px, &x, 16); memcpy(py, &y, 16);
    px[1] = (px[1] & 0x7fffffffffffffffull) | (py[1] & 0x8000000000000000ull);
    long double r; memcpy(&r, px, 16); return r;
}

/* ── fmax / fmin ─────────────────────────────────────────────────────────── */
float       fmaxf(float x, float y)             { return (x != x) ? y : (y != y) ? x : (x > y ? x : y); }
double      fmax(double x, double y)            { return (x != x) ? y : (y != y) ? x : (x > y ? x : y); }
long double fmaxl(long double x, long double y) { return (x != x) ? y : (y != y) ? x : (x > y ? x : y); }
float       fminf(float x, float y)             { return (x != x) ? y : (y != y) ? x : (x < y ? x : y); }
double      fmin(double x, double y)            { return (x != x) ? y : (y != y) ? x : (x < y ? x : y); }
long double fminl(long double x, long double y) { return (x != x) ? y : (y != y) ? x : (x < y ? x : y); }

/* ── fdim ────────────────────────────────────────────────────────────────── */
float       fdimf(float x, float y)             { return (x > y) ? (x - y) : 0.0f; }
double      fdim(double x, double y)            { return (x > y) ? (x - y) : 0.0; }
long double fdiml(long double x, long double y) { return (x > y) ? (x - y) : 0.0L; }

/* ── floor / ceil / trunc / round ────────────────────────────────────────── */
double      floor(double x)  { long long i = (long long)x; return (double)(x < (double)i ? i - 1 : i); }
float       floorf(float x)  { int i = (int)x; return (float)(x < (float)i ? i - 1 : i); }
long double floorl(long double x) { long long i = (long long)x; return (long double)(x < (long double)i ? i - 1 : i); }

double      ceil(double x)   { long long i = (long long)x; return (double)(x > (double)i ? i + 1 : i); }
float       ceilf(float x)   { int i = (int)x; return (float)(x > (float)i ? i + 1 : i); }
long double ceill(long double x) { long long i = (long long)x; return (long double)(x > (long double)i ? i + 1 : i); }

double      trunc(double x)  { return (double)(long long)x; }
float       truncf(float x)  { return (float)(int)x; }
long double truncl(long double x) { return (long double)(long long)x; }

double round(double x) {
    if (x >= 0.0)  return floor(x + 0.5);
    return ceil(x - 0.5);
}
float roundf(float x) {
    if (x >= 0.0f) return floorf(x + 0.5f);
    return ceilf(x - 0.5f);
}
long double roundl(long double x) {
    if (x >= 0.0L) return floorl(x + 0.5L);
    return ceill(x - 0.5L);
}

long        lround(double x)      { return (long)round(x); }
long        lroundf(float x)      { return (long)roundf(x); }
long        lroundl(long double x){ return (long)roundl(x); }
long long   llround(double x)     { return (long long)round(x); }
long long   llroundf(float x)     { return (long long)roundf(x); }
long long   llroundl(long double x){ return (long long)roundl(x); }

double      rint(double x)        { return round(x); }
float       rintf(float x)        { return roundf(x); }
long double rintl(long double x)  { return roundl(x); }
long        lrint(double x)       { return (long)rint(x); }
long        lrintf(float x)       { return (long)rintf(x); }
long        lrintl(long double x) { return (long)rintl(x); }
long long   llrint(double x)      { return (long long)rint(x); }
long long   llrintf(float x)      { return (long long)rintf(x); }
long long   llrintl(long double x){ return (long long)rintl(x); }

double      nearbyint(double x)       { return rint(x); }
float       nearbyintf(float x)       { return rintf(x); }
long double nearbyintl(long double x) { return rintl(x); }

/* ── scalbn / ldexp (multiply by 2^n) ───────────────────────────────────── */
double scalbn(double x, int n) {
    /* Use the exponent field: bias 1023, bits [62:52] */
    if (n > 1023)  n = 1023;
    if (n < -1022) n = -1022;
    unsigned long long exp_bits = (unsigned long long)(n + 1023) << 52;
    double scale = _bits_double(exp_bits);
    return x * scale;
}
float scalbnf(float x, int n) {
    if (n > 127)   n = 127;
    if (n < -126)  n = -126;
    unsigned exp_bits = (unsigned)(n + 127) << 23;
    float scale = _bits_float(exp_bits);
    return x * scale;
}
long double scalbnl(long double x, int n) { return (long double)scalbn((double)x, n); }

double      scalbln(double x, long n)      { return scalbn(x, (int)n); }
float       scalblnf(float x, long n)      { return scalbnf(x, (int)n); }
long double scalblnl(long double x, long n){ return scalbnl(x, (int)n); }

double      ldexp(double x, int e)         { return scalbn(x, e); }
float       ldexpf(float x, int e)         { return scalbnf(x, e); }
long double ldexpl(long double x, int e)   { return scalbnl(x, e); }

/* ── logb / ilogb ────────────────────────────────────────────────────────── */
double logb(double x) {
    unsigned long long b = (_double_bits(x) >> 52) & 0x7ff;
    return (b == 0) ? (double)FP_ILOGB0 : (double)((int)b - 1023);
}
float logbf(float x) {
    unsigned b = (_float_bits(x) >> 23) & 0xff;
    return (b == 0) ? (float)FP_ILOGB0 : (float)((int)b - 127);
}
long double logbl(long double x) { return (long double)logb((double)x); }

int ilogb(double x)      { return (int)logb(x); }
int ilogbf(float x)      { return (int)logbf(x); }
int ilogbl(long double x){ return (int)logb((double)x); }

/* ── frexp ───────────────────────────────────────────────────────────────── */
double frexp(double x, int *exp) {
    unsigned long long bits = _double_bits(x);
    int e = (int)((bits >> 52) & 0x7ff);
    if (e == 0) { *exp = 0; return x; }
    if (e == 0x7ff) { *exp = 0; return x; }
    *exp = e - 1022;
    bits = (bits & 0x800fffffffffffffull) | 0x3fe0000000000000ull;
    return _bits_double(bits);
}
float frexpf(float x, int *exp) {
    unsigned bits = _float_bits(x);
    int e = (int)((bits >> 23) & 0xff);
    if (e == 0) { *exp = 0; return x; }
    if (e == 0xff) { *exp = 0; return x; }
    *exp = e - 126;
    bits = (bits & 0x807fffffu) | 0x3f000000u;
    return _bits_float(bits);
}
long double frexpl(long double x, int *exp) { return (long double)frexp((double)x, exp); }

/* ── fmod ────────────────────────────────────────────────────────────────── */
double fmod(double x, double y) {
    if (y == 0.0) return __builtin_nan("");
    double q = trunc(x / y);
    return x - q * y;
}
float fmodf(float x, float y) {
    if (y == 0.0f) return __builtin_nanf("");
    float q = truncf(x / y);
    return x - q * y;
}
long double fmodl(long double x, long double y) {
    if (y == 0.0L) return __builtin_nanl("");
    long double q = truncl(x / y);
    return x - q * y;
}

/* ── remainder / remquo ──────────────────────────────────────────────────── */
double remainder(double x, double y) {
    double q = round(x / y);
    return x - q * y;
}
float remainderf(float x, float y) {
    float q = roundf(x / y);
    return x - q * y;
}
long double remainderl(long double x, long double y) {
    long double q = roundl(x / y);
    return x - q * y;
}
double remquo(double x, double y, int *q) {
    double qf = round(x / y);
    *q = (int)qf;
    return x - qf * y;
}
float remquof(float x, float y, int *q) {
    float qf = roundf(x / y);
    *q = (int)qf;
    return x - qf * y;
}
long double remquol(long double x, long double y, int *q) {
    long double qf = roundl(x / y);
    *q = (int)qf;
    return x - qf * y;
}

/* ── fma ─────────────────────────────────────────────────────────────────── */
double      fma(double x, double y, double z)            { return x * y + z; }
float       fmaf(float x, float y, float z)              { return x * y + z; }
long double fmal(long double x, long double y, long double z) { return x * y + z; }

/* ── nextafter / nexttoward ──────────────────────────────────────────────── */
double nextafter(double x, double y) {
    if (x == y) return y;
    unsigned long long bits = _double_bits(x);
    if (x < y) bits++; else bits--;
    return _bits_double(bits);
}
float nextafterf(float x, float y) {
    if (x == y) return y;
    unsigned bits = _float_bits(x);
    if (x < y) bits++; else bits--;
    return _bits_float(bits);
}
long double nextafterl(long double x, long double y) { return (long double)nextafter((double)x,(double)y); }
double      nexttoward(double x, long double y)  { return nextafter(x, (double)y); }
float       nexttowardf(float x, long double y)  { return nextafterf(x, (float)y); }
long double nexttowardl(long double x, long double y) { return nextafterl(x, y); }

/* ── nan ─────────────────────────────────────────────────────────────────── */
double      nan(const char *s)  { (void)s; return __builtin_nan(""); }
float       nanf(const char *s) { (void)s; return __builtin_nanf(""); }
long double nanl(const char *s) { (void)s; return __builtin_nanl(""); }

/* ── Transcendental functions — delegate to GCC builtins ─────────────────── */
/* GCC __builtin_* forms are resolved via libgcc soft-float at link time.     */

double      sqrt(double x)        { return __builtin_sqrt(x); }
float       sqrtf(float x)        { return __builtin_sqrtf(x); }
long double sqrtl(long double x)  { return __builtin_sqrtl(x); }

double      sin(double x)         { return __builtin_sin(x); }
float       sinf(float x)         { return __builtin_sinf(x); }
long double sinl(long double x)   { return __builtin_sinl(x); }

double      cos(double x)         { return __builtin_cos(x); }
float       cosf(float x)         { return __builtin_cosf(x); }
long double cosl(long double x)   { return __builtin_cosl(x); }

double      tan(double x)         { return __builtin_tan(x); }
float       tanf(float x)         { return __builtin_tanf(x); }
long double tanl(long double x)   { return __builtin_tanl(x); }

double      asin(double x)        { return __builtin_asin(x); }
float       asinf(float x)        { return __builtin_asinf(x); }
long double asinl(long double x)  { return __builtin_asinl(x); }

double      acos(double x)        { return __builtin_acos(x); }
float       acosf(float x)        { return __builtin_acosf(x); }
long double acosl(long double x)  { return __builtin_acosl(x); }

double      atan(double x)        { return __builtin_atan(x); }
float       atanf(float x)        { return __builtin_atanf(x); }
long double atanl(long double x)  { return __builtin_atanl(x); }

double      atan2(double y,double x)    { return __builtin_atan2(y,x); }
float       atan2f(float y,float x)     { return __builtin_atan2f(y,x); }
long double atan2l(long double y,long double x) { return __builtin_atan2l(y,x); }

double      sinh(double x)        { return __builtin_sinh(x); }
float       sinhf(float x)        { return __builtin_sinhf(x); }
long double sinhl(long double x)  { return __builtin_sinhl(x); }

double      cosh(double x)        { return __builtin_cosh(x); }
float       coshf(float x)        { return __builtin_coshf(x); }
long double coshl(long double x)  { return __builtin_coshl(x); }

double      tanh(double x)        { return __builtin_tanh(x); }
float       tanhf(float x)        { return __builtin_tanhf(x); }
long double tanhl(long double x)  { return __builtin_tanhl(x); }

double      asinh(double x)       { return __builtin_asinh(x); }
float       asinhf(float x)       { return __builtin_asinhf(x); }
long double asinhl(long double x) { return __builtin_asinhl(x); }

double      acosh(double x)       { return __builtin_acosh(x); }
float       acoshf(float x)       { return __builtin_acoshf(x); }
long double acoshl(long double x) { return __builtin_acoshl(x); }

double      atanh(double x)       { return __builtin_atanh(x); }
float       atanhf(float x)       { return __builtin_atanhf(x); }
long double atanhl(long double x) { return __builtin_atanhl(x); }

double      exp(double x)         { return __builtin_exp(x); }
float       expf(float x)         { return __builtin_expf(x); }
long double expl(long double x)   { return __builtin_expl(x); }

double      exp2(double x)        { return __builtin_exp2(x); }
float       exp2f(float x)        { return __builtin_exp2f(x); }
long double exp2l(long double x)  { return __builtin_exp2l(x); }

double      expm1(double x)       { return __builtin_expm1(x); }
float       expm1f(float x)       { return __builtin_expm1f(x); }
long double expm1l(long double x) { return __builtin_expm1l(x); }

double      log(double x)         { return __builtin_log(x); }
float       logf(float x)         { return __builtin_logf(x); }
long double logl(long double x)   { return __builtin_logl(x); }

double      log2(double x)        { return __builtin_log2(x); }
float       log2f(float x)        { return __builtin_log2f(x); }
long double log2l(long double x)  { return __builtin_log2l(x); }

double      log10(double x)       { return __builtin_log10(x); }
float       log10f(float x)       { return __builtin_log10f(x); }
long double log10l(long double x) { return __builtin_log10l(x); }

double      log1p(double x)       { return __builtin_log1p(x); }
float       log1pf(float x)       { return __builtin_log1pf(x); }
long double log1pl(long double x) { return __builtin_log1pl(x); }

double      pow(double x, double y)      { return __builtin_pow(x,y); }
float       powf(float x, float y)       { return __builtin_powf(x,y); }
long double powl(long double x, long double y) { return __builtin_powl(x,y); }

double      cbrt(double x)        { return __builtin_cbrt(x); }
float       cbrtf(float x)        { return __builtin_cbrtf(x); }
long double cbrtl(long double x)  { return __builtin_cbrtl(x); }

double      hypot(double x, double y)         { return __builtin_hypot(x,y); }
float       hypotf(float x, float y)          { return __builtin_hypotf(x,y); }
long double hypotl(long double x, long double y) { return __builtin_hypotl(x,y); }

double      erf(double x)         { return __builtin_erf(x); }
float       erff(float x)         { return __builtin_erff(x); }
long double erfl(long double x)   { return __builtin_erfl(x); }

double      erfc(double x)        { return __builtin_erfc(x); }
float       erfcf(float x)        { return __builtin_erfcf(x); }
long double erfcl(long double x)  { return __builtin_erfcl(x); }

double      tgamma(double x)      { return __builtin_tgamma(x); }
float       tgammaf(float x)      { return __builtin_tgammaf(x); }
long double tgammal(long double x){ return __builtin_tgammal(x); }

double      lgamma(double x)      { return __builtin_lgamma(x); }
float       lgammaf(float x)      { return __builtin_lgammaf(x); }
long double lgammal(long double x){ return __builtin_lgammal(x); }

/* Complex functions are in complex.c */
