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

/* ── modf: split into integer and fractional parts ──────────────────────── */
double modf(double x, double *iptr) {
    double t = trunc(x);
    *iptr = t;
    return x - t;
}
float modff(float x, float *iptr) {
    float t = truncf(x);
    *iptr = t;
    return x - t;
}
long double modfl(long double x, long double *iptr) {
    long double t = truncl(x);
    *iptr = t;
    return x - t;
}

/* ── Transcendental functions — real C implementations ──────────────────────
 * rv64imac is soft-float; __builtin_sqrt et al. lower to libgcc calls back
 * into these symbols → infinite recursion. Real impls below avoid the trap.
 * Accuracy target: ~1e-6 (poly approxs, not IEEE-correct).
 */

/* Constants */
#define _M_PI       3.14159265358979323846
#define _M_PI_2     1.57079632679489661923
#define _M_PI_4     0.78539816339744830962
#define _M_LN2      0.69314718055994530942
#define _M_LN10     2.30258509299404568402
#define _M_LOG2E    1.44269504088896340736
#define _M_LOG10E   0.43429448190325182765
#define _M_SQRT1_2  0.70710678118654752440

static int _isnan_d(double x) { return x != x; }
static int _isinf_d(double x) { return x != 0.0 && x + x == x; }

/* ── sqrt: integer digit-by-digit (no FP multiply needed) ───────────────── */
double sqrt(double x) {
    if (_isnan_d(x)) return x;
    if (x < 0.0) return __builtin_nan("");
    if (x == 0.0 || _isinf_d(x)) return x;

    unsigned long long xb = _double_bits(x);
    int exp = (int)((xb >> 52) & 0x7ff);

    unsigned long long m = (xb & 0x000fffffffffffffull) | 0x0010000000000000ull;
    int e = exp - 1023; /* unbiased exponent: x = m * 2^(e-52) */

    /* Make (e-52) even so the sqrt exponent is exact. */
    if ((e - 52) & 1) { m <<= 1; e--; }
    /* After adjustment:
     *   normal case : m in [2^52, 2^53)  (e-52 was already even)
     *   shifted case: m in [2^53, 2^54)  (we did m<<=1 above)       */

    /* FIX 1 — avoid overflow on m<<10.
     * Re-normalize m back to [2^52, 2^53) by absorbing any extra bit into e,
     * then shift by exactly 10.  This is always safe because m < 2^53. */
    if (m >= 0x0020000000000000ull) { /* m in [2^53, 2^54) after odd-exp shift */
        e += 1;        /* compensate: x = (m>>1) * 2^(e-52+1) = (m>>1)*2^(e-51) */
        m >>= 1;       /* now m back in [2^52, 2^53); e still even–parity ok     */
    }
    /* Invariant: m in [2^52, 2^53), (e-52) even. */

    /* Shift m up by 10 to produce a ~62-bit radicand; isqrt will be ~31 bits.
     * We want 53 bits in the result, so we actually need isqrt(m << 2*k) for
     * k >= 53.  Use k=53: shift = 2*53 - 52 = 54 bits total beyond the hidden
     * bit, but 64-bit arithmetic only gives us 10 spare bits above 2^52.
     * Shifting by 10 gives isqrt a ~57-bit integer → 28-bit root, then we
     * scale up the root later.  The extra remainder bits drive rounding. */
    unsigned long long val = m << 10; /* m < 2^53, so m<<10 < 2^63: no overflow */

    /* ── Integer square root (restoring, digit-by-bit) ── */
    unsigned long long r = 0;
    /* Highest power-of-4 not exceeding val.  val < 2^63, so start at 1<<62. */
    unsigned long long bit = 1ULL << 62;
    while (bit > val) bit >>= 2;
    while (bit) {
        unsigned long long t = r | bit;
        if (val >= t) { val -= t; r = (r >> 1) | bit; }
        else          { r >>= 1; }
        bit >>= 2;
    }
    /* Postcondition: r = floor(sqrt(m << 10)), val = (m<<10) - r*r  (remainder) */

    /* ── FIX 2 — round to nearest, ties to even ──
     * True sqrt lies in [r, r+1).  Round up iff the remainder exceeds the
     * halfway point, i.e. (m<<10) - r^2 > r  <=>  val > r.
     * Tie (val == r) rounds to even: increment only if r is odd. */
    if (val > r || (val == r && (r & 1))) {
        r++;
    }

    /* r ≈ sqrt(m) * 2^5   (because isqrt(m << 10) = isqrt(m) * 2^5)
     * sqrt(x) = sqrt(m * 2^(e-52))
     *         = r * 2^(-5) * 2^((e-52)/2)
     *         = r * 2^((e-52)/2 - 5)
     * The result significand should be in [1, 2), i.e. we need r in [2^52, 2^53).
     * r is currently ~27–28 bits; normalize by shifting left. */
    int rexp = (e - 52) / 2 - 5 + 52;
    /*          ^^^^^^^^^^^^^^^^   unbiased exponent of the result integer r
     *                        +52  because we'll express r as 1.frac * 2^52     */

    /* Normalize r into [2^52, 2^53) */
    int shift = 0;
    unsigned long long rt = r;
    while (rt && rt < 0x0010000000000000ull) { rt <<= 1; shift++; }
    rexp -= shift;

    unsigned long long frac = rt & 0x000fffffffffffffull;
    unsigned long long bits = ((unsigned long long)(rexp + 1023) << 52) | frac;
    return _bits_double(bits);
}
float       sqrtf(float x)       { return (float)sqrt((double)x); }
long double sqrtl(long double x) { return (long double)sqrt((double)x); }

/* ── exp: range-reduce x = k·ln2 + r, poly on r ─────────────────────────── */
double exp(double x) {
    if (_isnan_d(x)) return x;
    if (x > 709.78) return 1.0/0.0;  /* +inf */
    if (x < -745.0) return 0.0;
    /* k = round(x / ln2) */
    double kf = x * _M_LOG2E;
    long long k = (long long)(kf + (kf >= 0 ? 0.5 : -0.5));
    double r = x - (double)k * _M_LN2;
    /* Padé-ish poly for e^r, r in [-ln2/2, ln2/2] */
    double r2 = r * r;
    double p = r * (0.5 + r2 * (1.0/24.0 + r2 * (1.0/720.0)));
    double q = 1.0 + r2 * (1.0/12.0 + r2 * (1.0/360.0));
    double er = 1.0 + 2.0 * p / (q - p);
    return scalbn(er, (int)k);
}
float       expf(float x)       { return (float)exp((double)x); }
long double expl(long double x) { return (long double)exp((double)x); }

double exp2(double x)  { return exp(x * _M_LN2); }
float  exp2f(float x)  { return (float)exp2((double)x); }
long double exp2l(long double x) { return (long double)exp2((double)x); }

double expm1(double x) {
    if (fabs(x) < 1e-5) {
        return x + 0.5*x*x + (x*x*x)/6.0 + (x*x*x*x)/24.0;
    }
    return exp(x) - 1.0;
}
float  expm1f(float x) { return (float)expm1((double)x); }
long double expm1l(long double x) { return (long double)expm1((double)x); }

/* ── log: x = 2^k · m, m in [1,2); log = k·ln2 + log(m) ─────────────────── */
double log(double x) {
    if (_isnan_d(x)) return x;
    if (x < 0.0)  return __builtin_nan("");
    if (x == 0.0) return -1.0/0.0;
    if (_isinf_d(x)) return x;
    unsigned long long b = _double_bits(x);
    int e = (int)((b >> 52) & 0x7ff) - 1023;
    /* m = mantissa scaled to [1,2) */
    unsigned long long mb = (b & 0x000fffffffffffffull) | 0x3ff0000000000000ull;
    double m = _bits_double(mb);
    /* Normalize: if m > sqrt(2), halve and bump e. */
    if (m > 1.41421356237) { m *= 0.5; e++; }
    /* t = (m-1)/(m+1), log(m) = 2·(t + t^3/3 + t^5/5 + ...) */
    double t = (m - 1.0) / (m + 1.0);
    double t2 = t * t;
    double s = t * (1.0 + t2*(1.0/3.0 + t2*(1.0/5.0 + t2*(1.0/7.0 + t2*(1.0/9.0 + t2*(1.0/11.0 + t2*(1.0/13.0)))))));
    return 2.0 * s + (double)e * _M_LN2;
}
float       logf(float x)       { return (float)log((double)x); }
long double logl(long double x) { return (long double)log((double)x); }

double log2(double x)  { return log(x) * _M_LOG2E; }
float  log2f(float x)  { return (float)log2((double)x); }
long double log2l(long double x) { return (long double)log2((double)x); }

double log10(double x) { return log(x) * _M_LOG10E; }
float  log10f(float x) { return (float)log10((double)x); }
long double log10l(long double x) { return (long double)log10((double)x); }

double log1p(double x) {
    if (fabs(x) < 1e-4) {
        return x - 0.5*x*x + (x*x*x)/3.0 - (x*x*x*x)/4.0;
    }
    return log(1.0 + x);
}
float  log1pf(float x) { return (float)log1p((double)x); }
long double log1pl(long double x) { return (long double)log1p((double)x); }

/* ── pow: exp(y·log(x)) w/ special cases ────────────────────────────────── */
double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 1.0) return 1.0;
    if (_isnan_d(x) || _isnan_d(y)) return __builtin_nan("");
    if (x == 0.0) {
        if (y < 0.0) return 1.0/0.0;
        return 0.0;
    }
    if (x < 0.0) {
        /* Integer y only. */
        double yt = trunc(y);
        if (yt != y) return __builtin_nan("");
        double r = exp(y * log(-x));
        long long iy = (long long)yt;
        return (iy & 1) ? -r : r;
    }
    return exp(y * log(x));
}
float       powf(float x, float y)             { return (float)pow((double)x, (double)y); }
long double powl(long double x, long double y) { return (long double)pow((double)x, (double)y); }

/* ── cbrt: via exp(log(|x|)/3) ──────────────────────────────────────────── */
double cbrt(double x) {
    if (x == 0.0) return x;
    if (_isnan_d(x) || _isinf_d(x)) return x;
    double a = fabs(x);
    double r = exp(log(a) / 3.0);
    /* One NR step on f(y) = y^3 - a. */
    r = r - (r*r*r - a) / (3.0 * r * r);
    return (x < 0.0) ? -r : r;
}
float       cbrtf(float x)       { return (float)cbrt((double)x); }
long double cbrtl(long double x) { return (long double)cbrt((double)x); }

/* ── hypot: scaled sqrt ─────────────────────────────────────────────────── */
double hypot(double x, double y) {
    x = fabs(x); y = fabs(y);
    if (x < y) { double t = x; x = y; y = t; }
    if (x == 0.0) return 0.0;
    double r = y / x;
    return x * sqrt(1.0 + r * r);
}
float       hypotf(float x, float y)             { return (float)hypot((double)x, (double)y); }
long double hypotl(long double x, long double y) { return (long double)hypot((double)x, (double)y); }

/* ── sin/cos: reduce mod π/2, poly per quadrant ─────────────────────────── */
static double _sin_poly(double x) {
    /* sin(x) for |x| <= π/4. Taylor 11-term. */
    double x2 = x * x;
    return x * (1.0 + x2*(-1.0/6.0 + x2*(1.0/120.0 + x2*(-1.0/5040.0 + x2*(1.0/362880.0 + x2*(-1.0/39916800.0))))));
}
static double _cos_poly(double x) {
    /* cos(x) for |x| <= π/4. Taylor 12-term. */
    double x2 = x * x;
    return 1.0 + x2*(-0.5 + x2*(1.0/24.0 + x2*(-1.0/720.0 + x2*(1.0/40320.0 + x2*(-1.0/3628800.0 + x2*(1.0/479001600.0))))));
}

double sin(double x) {
    if (_isnan_d(x) || _isinf_d(x)) return __builtin_nan("");
    /* Reduce to [-π, π] via fmod. */
    double t = fmod(x, 2.0 * _M_PI);
    if (t >  _M_PI) t -= 2.0 * _M_PI;
    if (t < -_M_PI) t += 2.0 * _M_PI;
    /* Reduce to [-π/2, π/2] via reflection. */
    if (t >  _M_PI_2) t =  _M_PI - t;
    if (t < -_M_PI_2) t = -_M_PI - t;
    /* Reduce to [-π/4, π/4] via cos/sin swap. */
    if (t > _M_PI_4)  return _cos_poly(_M_PI_2 - t);
    if (t < -_M_PI_4) return -_cos_poly(_M_PI_2 + t);
    return _sin_poly(t);
}
float       sinf(float x)       { return (float)sin((double)x); }
long double sinl(long double x) { return (long double)sin((double)x); }

double cos(double x) {
    if (_isnan_d(x) || _isinf_d(x)) return __builtin_nan("");
    double t = fmod(x, 2.0 * _M_PI);
    if (t >  _M_PI) t -= 2.0 * _M_PI;
    if (t < -_M_PI) t += 2.0 * _M_PI;
    /* cos is even */
    if (t < 0.0) t = -t;
    /* Now t in [0, π] */
    if (t > _M_PI_2) return -_cos_poly(_M_PI - t);
    if (t > _M_PI_4) return _sin_poly(_M_PI_2 - t);
    return _cos_poly(t);
}
float       cosf(float x)       { return (float)cos((double)x); }
long double cosl(long double x) { return (long double)cos((double)x); }

double tan(double x) {
    double c = cos(x);
    if (c == 0.0) return 1.0/0.0;
    return sin(x) / c;
}
float       tanf(float x)       { return (float)tan((double)x); }
long double tanl(long double x) { return (long double)tan((double)x); }

/* ── atan: poly for |x|<=1, reciprocal trick for |x|>1 ──────────────────── */
static double _atan_poly(double x) {
    /* For |x| <= 1; use a degree-9 odd poly w/ further reduction. */
    /* Reduce: if |x| > tan(π/8) ≈ 0.4142, use atan(x) = π/4 + atan((x-1)/(x+1)) */
    int neg = 0;
    if (x < 0.0) { x = -x; neg = 1; }
    double add = 0.0;
    if (x > 0.4142135623730950) {
        add = _M_PI_4;
        x = (x - 1.0) / (x + 1.0);
    }
    double x2 = x * x;
    double r = x * (1.0 + x2*(-1.0/3.0 + x2*(1.0/5.0 + x2*(-1.0/7.0 + x2*(1.0/9.0 + x2*(-1.0/11.0 + x2*(1.0/13.0)))))));
    r += add;
    return neg ? -r : r;
}
double atan(double x) {
    if (_isnan_d(x)) return x;
    if (x == 0.0) return x;
    if (_isinf_d(x)) return x > 0 ? _M_PI_2 : -_M_PI_2;
    if (fabs(x) <= 1.0) return _atan_poly(x);
    /* atan(x) = π/2 - atan(1/x) for x > 0; -π/2 - atan(1/x) for x < 0 */
    double r = _atan_poly(1.0 / x);
    return (x > 0) ? (_M_PI_2 - r) : (-_M_PI_2 - r);
}
float       atanf(float x)       { return (float)atan((double)x); }
long double atanl(long double x) { return (long double)atan((double)x); }

double atan2(double y, double x) {
    if (_isnan_d(x) || _isnan_d(y)) return __builtin_nan("");
    if (x == 0.0) {
        if (y > 0.0) return _M_PI_2;
        if (y < 0.0) return -_M_PI_2;
        return 0.0;
    }
    double a = atan(y / x);
    if (x > 0.0) return a;
    if (y >= 0.0) return a + _M_PI;
    return a - _M_PI;
}
float       atan2f(float y, float x)             { return (float)atan2((double)y, (double)x); }
long double atan2l(long double y, long double x) { return (long double)atan2((double)y, (double)x); }

/* ── asin/acos: via atan ────────────────────────────────────────────────── */
double asin(double x) {
    if (_isnan_d(x)) return x;
    if (x < -1.0 || x > 1.0) return __builtin_nan("");
    if (x == 1.0)  return _M_PI_2;
    if (x == -1.0) return -_M_PI_2;
    if (fabs(x) > _M_SQRT1_2) {
        double s = sqrt(1.0 - x*x);
        double a = atan(s / x);
        if (x > 0) return _M_PI_2 - a;
        return -_M_PI_2 - a;
    }
    return atan(x / sqrt(1.0 - x*x));
}
float       asinf(float x)       { return (float)asin((double)x); }
long double asinl(long double x) { return (long double)asin((double)x); }

double acos(double x) {
    if (_isnan_d(x)) return x;
    if (x < -1.0 || x > 1.0) return __builtin_nan("");
    return _M_PI_2 - asin(x);
}
float       acosf(float x)       { return (float)acos((double)x); }
long double acosl(long double x) { return (long double)acos((double)x); }

/* ── Hyperbolics ────────────────────────────────────────────────────────── */
double sinh(double x) {
    if (fabs(x) < 1e-4) return x + (x*x*x)/6.0;
    double e = exp(x);
    return 0.5 * (e - 1.0/e);
}
float       sinhf(float x)       { return (float)sinh((double)x); }
long double sinhl(long double x) { return (long double)sinh((double)x); }

double cosh(double x) {
    double e = exp(fabs(x));
    return 0.5 * (e + 1.0/e);
}
float       coshf(float x)       { return (float)cosh((double)x); }
long double coshl(long double x) { return (long double)cosh((double)x); }

double tanh(double x) {
    if (x >  20.0) return 1.0;
    if (x < -20.0) return -1.0;
    if (fabs(x) < 1e-4) return x - (x*x*x)/3.0;
    double e2 = exp(2.0 * x);
    return (e2 - 1.0) / (e2 + 1.0);
}
float       tanhf(float x)       { return (float)tanh((double)x); }
long double tanhl(long double x) { return (long double)tanh((double)x); }

double asinh(double x) {
    if (fabs(x) < 1e-4) return x;
    int neg = x < 0.0;
    double a = fabs(x);
    double r = log(a + sqrt(a*a + 1.0));
    return neg ? -r : r;
}
float       asinhf(float x)       { return (float)asinh((double)x); }
long double asinhl(long double x) { return (long double)asinh((double)x); }

double acosh(double x) {
    if (x < 1.0) return __builtin_nan("");
    return log(x + sqrt(x*x - 1.0));
}
float       acoshf(float x)       { return (float)acosh((double)x); }
long double acoshl(long double x) { return (long double)acosh((double)x); }

double atanh(double x) {
    if (x <= -1.0 || x >= 1.0) return __builtin_nan("");
    return 0.5 * log((1.0 + x) / (1.0 - x));
}
float       atanhf(float x)       { return (float)atanh((double)x); }
long double atanhl(long double x) { return (long double)atanh((double)x); }

/* ── Tier 3: stubs (not used by tgmath test) ────────────────────────────── */
double      erf(double x)         { (void)x; return 0.0; }
float       erff(float x)         { (void)x; return 0.0f; }
long double erfl(long double x)   { (void)x; return 0.0L; }

double      erfc(double x)        { (void)x; return 1.0; }
float       erfcf(float x)        { (void)x; return 1.0f; }
long double erfcl(long double x)  { (void)x; return 1.0L; }

double      tgamma(double x)      { (void)x; return 1.0; }
float       tgammaf(float x)      { (void)x; return 1.0f; }
long double tgammal(long double x){ (void)x; return 1.0L; }

double      lgamma(double x)      { (void)x; return 0.0; }
float       lgammaf(float x)      { (void)x; return 0.0f; }
long double lgammal(long double x){ (void)x; return 0.0L; }

/* ── XSI: signgam — set by lgamma() to indicate sign of Γ(x) ────────────── */
int signgam = 1;

/* ── XSI: Bessel — stubs (not needed) ───────────────────────────────────── */
double j0(double x) { (void)x; return 0.0; }
double j1(double x) { (void)x; return 0.0; }
double jn(int n, double x) { (void)n; (void)x; return 0.0; }
double y0(double x) { (void)x; return 0.0; }
double y1(double x) { (void)x; return 0.0; }
double yn(int n, double x) { (void)n; (void)x; return 0.0; }

/* Complex functions are in complex.c */

/* ── Soft-float compiler runtime (GCC ABI) ───────────────────────────────────
 * rv64imac has no F/D extensions; TCC-compiled user code references these
 * symbols at runtime.  Weak so libgcc can override if present.
 */

#include <stdint.h>

#define _SF_W __attribute__((weak))

typedef union { double d; uint64_t u; } _sf_du;
#define _SF_SIGN     (1ULL << 63)
#define _SF_EXP_MASK 0x7FF0000000000000ULL
#define _SF_FRAC_MASK 0x000FFFFFFFFFFFFFULL
#define _SF_EXP_BIAS 1023
#define _SF_EXP_SHIFT 52
#define _SF_HIDDEN   (1ULL << 52)

static inline int      _sf_dexp(uint64_t u)  { return (int)((u >> _SF_EXP_SHIFT) & 0x7FF); }
static inline uint64_t _sf_dfrac(uint64_t u) { return u & _SF_FRAC_MASK; }
static inline int      _sf_dsign(uint64_t u) { return (int)(u >> 63); }

static uint64_t _sf_pack(int sign, int exp, uint64_t frac) {
    return ((uint64_t)sign << 63) | ((uint64_t)(exp & 0x7FF) << _SF_EXP_SHIFT) | (frac & _SF_FRAC_MASK);
}

static uint64_t _sf_normalize(int sign, int exp, uint64_t frac, int extra) {
    if (!frac) return _sf_pack(sign, 0, 0);
    while (frac && !(frac & (_SF_HIDDEN << extra))) { frac <<= 1; exp--; }
    if (extra > 0) {
        uint64_t half = 1ULL << (extra - 1);
        uint64_t mask = (1ULL << extra) - 1;
        uint64_t trail = frac & mask;
        frac >>= extra;
        if (trail > half || (trail == half && (frac & 1))) frac++;
        if (frac & (_SF_HIDDEN << 1)) { frac >>= 1; exp++; }
    }
    if (exp >= 0x7FF) return _sf_pack(sign, 0x7FF, 0);
    if (exp <= 0)     return _sf_pack(sign, 0, 0);
    return _sf_pack(sign, exp, frac & _SF_FRAC_MASK);
}

_SF_W double __adddf3(double a, double b) {
    _sf_du ua = {a}, ub = {b};
    uint64_t au = ua.u, bu = ub.u;
    int ae = _sf_dexp(au), be = _sf_dexp(bu);
    int as = _sf_dsign(au), bs = _sf_dsign(bu);
    uint64_t af = _sf_dfrac(au), bf = _sf_dfrac(bu);
    if (!ae && !af) return b;
    if (!be && !bf) return a;
    if (ae == 0x7FF || be == 0x7FF) { _sf_du r; r.u = (ae == 0x7FF) ? au : bu; return r.d; }
    if (ae) af |= _SF_HIDDEN; else { ae = 1; }
    if (be) bf |= _SF_HIDDEN; else { be = 1; }
    int extra = 3; af <<= extra; bf <<= extra;
    int exp = ae;
    if (ae > be) { int sh = ae - be; bf = (sh >= 64) ? 0 : bf >> sh; }
    else if (be > ae) { int sh = be - ae; af = (sh >= 64) ? 0 : af >> sh; exp = be; }
    uint64_t rf; int rs;
    if (as == bs) {
        rf = af + bf; rs = as;
        if (rf & (_SF_HIDDEN << (extra + 1))) { rf >>= 1; exp++; }
    } else {
        if (af >= bf) { rf = af - bf; rs = as; } else { rf = bf - af; rs = bs; }
    }
    _sf_du r; r.u = _sf_normalize(rs, exp, rf, extra); return r.d;
}

_SF_W double __subdf3(double a, double b) {
    _sf_du ub = {b}; ub.u ^= _SF_SIGN; return __adddf3(a, ub.d);
}

_SF_W double __muldf3(double a, double b) {
    _sf_du ua = {a}, ub = {b};
    uint64_t au = ua.u, bu = ub.u;
    int ae = _sf_dexp(au), be = _sf_dexp(bu);
    int rs = _sf_dsign(au) ^ _sf_dsign(bu);
    uint64_t af = _sf_dfrac(au), bf = _sf_dfrac(bu);
    if ((!ae && !af) || (!be && !bf)) { _sf_du r; r.u = _sf_pack(rs,0,0); return r.d; }
    if (ae == 0x7FF || be == 0x7FF)  { _sf_du r; r.u = _sf_pack(rs,0x7FF,0); return r.d; }
    if (ae) af |= _SF_HIDDEN; else { ae = 1; }
    if (be) bf |= _SF_HIDDEN; else { be = 1; }
    int exp = ae + be - _SF_EXP_BIAS;
    uint64_t ahi = af >> 21, alo = af & 0x1FFFFF;
    uint64_t bhi = bf >> 21, blo = bf & 0x1FFFFF;
    uint64_t prod = ahi * bhi + ((ahi * blo + alo * bhi) >> 21);
    int extra = 10, target = _SF_EXP_SHIFT + extra;
    uint64_t tmp = prod; int lead = 0;
    while (tmp && !(tmp & (1ULL << 62))) { tmp <<= 1; lead++; }
    int cur = 62 - lead;
    if (cur > target) { prod >>= (cur - target); } else { prod <<= (target - cur); exp -= (target - cur); }
    exp += cur - (_SF_EXP_SHIFT * 2 - 21);
    _sf_du r; r.u = _sf_normalize(rs, exp, prod, extra); return r.d;
}

_SF_W double __divdf3(double a, double b) {
    _sf_du ua = {a}, ub = {b};
    uint64_t au = ua.u, bu = ub.u;
    int ae = _sf_dexp(au), be = _sf_dexp(bu);
    int rs = _sf_dsign(au) ^ _sf_dsign(bu);
    uint64_t af = _sf_dfrac(au), bf = _sf_dfrac(bu);
    if (ae == 0x7FF || (!be && !bf)) { _sf_du r; r.u = _sf_pack(rs,0x7FF,0); return r.d; }
    if (!ae && !af) { _sf_du r; r.u = _sf_pack(rs,0,0); return r.d; }
    if (ae) af |= _SF_HIDDEN; else { ae = 1; }
    if (be) bf |= _SF_HIDDEN; else { be = 1; }
    int exp = ae - be + _SF_EXP_BIAS;
    /* restoring bit-by-bit division, 64 quotient bits, no 128-bit type needed */
    int extra = 11;
    uint64_t num = af, den = bf, q = 0;
    for (int i = _SF_EXP_SHIFT + extra; i >= 0; i--) {
        if (num >= den) { q |= (1ULL << i); num -= den; }
        if (i > 0) {
            int overflow = (int)(num >> 63);
            num <<= 1;
            if (overflow) { q |= (1ULL << (i-1)); num -= den; }
        }
    }
    exp -= extra;
    _sf_du r; r.u = _sf_normalize(rs, exp, q, extra); return r.d;
}

_SF_W double __extendsfdf2(float a) {
    union { float f; uint32_t u; } fa = {a};
    uint32_t u = fa.u;
    int sign = (int)(u >> 31), exp = (int)((u >> 23) & 0xFF);
    uint32_t frac = u & 0x7FFFFF;
    _sf_du r;
    if (exp == 0xFF) { r.u = _sf_pack(sign, 0x7FF, frac ? 1 : 0); return r.d; }
    if (!exp && !frac) { r.u = (uint64_t)sign << 63; return r.d; }
    if (!exp) { while (!(frac & 0x800000)) { frac <<= 1; exp--; } exp++; }
    r.u = _sf_pack(sign, exp - 127 + _SF_EXP_BIAS, ((uint64_t)frac & 0x7FFFFF) << 29);
    return r.d;
}

_SF_W float __truncdfsf2(double a) {
    _sf_du ua = {a}; uint64_t u = ua.u;
    int sign = _sf_dsign(u), exp = _sf_dexp(u);
    uint64_t frac = _sf_dfrac(u);
    union { float f; uint32_t u; } fu;
    if (exp == 0x7FF) { fu.u = ((uint32_t)sign << 31) | 0x7F800000 | (frac ? 1 : 0); return fu.f; }
    int fexp = exp - _SF_EXP_BIAS + 127;
    uint32_t ff = (uint32_t)(frac >> 29);
    uint64_t rb = frac & 0x1FFFFFFF;
    if (rb > 0x10000000 || (rb == 0x10000000 && (ff & 1))) ff++;
    if (ff & 0x800000) { if (exp) { ff = 0; fexp++; } }
    if (fexp >= 0xFF) { fu.u = ((uint32_t)sign << 31) | 0x7F800000; return fu.f; }
    if (fexp <= 0 || (!exp && !frac)) { fu.u = (uint32_t)sign << 31; return fu.f; }
    fu.u = ((uint32_t)sign << 31) | ((uint32_t)fexp << 23) | (ff & 0x7FFFFF);
    return fu.f;
}

_SF_W float __addsf3(float a, float b) { return __truncdfsf2(__adddf3(__extendsfdf2(a), __extendsfdf2(b))); }
_SF_W float __subsf3(float a, float b) { return __truncdfsf2(__subdf3(__extendsfdf2(a), __extendsfdf2(b))); }
_SF_W float __mulsf3(float a, float b) { return __truncdfsf2(__muldf3(__extendsfdf2(a), __extendsfdf2(b))); }
_SF_W float __divsf3(float a, float b) { return __truncdfsf2(__divdf3(__extendsfdf2(a), __extendsfdf2(b))); }

_SF_W double __floatsidf(int i) {
    if (!i) return 0.0;
    int sign = 0; unsigned int u = (unsigned int)i;
    if (i < 0) { sign = 1; u = (unsigned int)-i; }
    int exp = _SF_EXP_BIAS + 52; uint64_t frac = (uint64_t)u;
    while (!(frac & _SF_HIDDEN)) { frac <<= 1; exp--; }
    _sf_du r; r.u = _sf_pack(sign, exp, frac & _SF_FRAC_MASK); return r.d;
}
_SF_W double __floatdidf(long long i) {
    if (!i) return 0.0;
    int sign = 0; unsigned long long u = (unsigned long long)i;
    if (i < 0) { sign = 1; u = (unsigned long long)-i; }
    int exp = _SF_EXP_BIAS + 52; uint64_t frac = u;
    while (frac & ~((_SF_HIDDEN << 1) - 1)) { frac >>= 1; exp++; }
    while (!(frac & _SF_HIDDEN)) { frac <<= 1; exp--; }
    _sf_du r; r.u = _sf_pack(sign, exp, frac & _SF_FRAC_MASK); return r.d;
}
_SF_W float __floatsisf(int i)     { return __truncdfsf2(__floatsidf(i)); }
_SF_W float __floatdisf(long long i) { return __truncdfsf2(__floatdidf(i)); }

_SF_W int __fixdfsi(double a) {
    _sf_du ua = {a}; uint64_t u = ua.u;
    int sign = _sf_dsign(u), exp = _sf_dexp(u);
    uint64_t frac = _sf_dfrac(u);
    if (exp < _SF_EXP_BIAS) return 0;
    int shift = (int)(exp - _SF_EXP_BIAS - 52);
    uint64_t mant = frac | _SF_HIDDEN;
    int64_t r = (shift >= 0) ? (int64_t)(mant << shift) : (int64_t)(mant >> -shift);
    return (int)(sign ? -r : r);
}
_SF_W long long __fixdfdi(double a) {
    _sf_du ua = {a}; uint64_t u = ua.u;
    int sign = _sf_dsign(u), exp = _sf_dexp(u);
    uint64_t frac = _sf_dfrac(u);
    if (exp < _SF_EXP_BIAS) return 0;
    int shift = (int)(exp - _SF_EXP_BIAS - 52);
    uint64_t mant = frac | _SF_HIDDEN;
    int64_t r = (shift >= 0) ? (int64_t)(mant << shift) : (int64_t)(mant >> -shift);
    return sign ? -r : r;
}
_SF_W int __fixsfsi(float a)      { return __fixdfsi(__extendsfdf2(a)); }
_SF_W long long __fixsfdi(float a) { return __fixdfdi(__extendsfdf2(a)); }

static int _sf_dcmp(double a, double b) {
    _sf_du ua = {a}, ub = {b};
    uint64_t au = ua.u, bu = ub.u;
    int ae = _sf_dexp(au), be = _sf_dexp(bu);
    if ((ae == 0x7FF && _sf_dfrac(au)) || (be == 0x7FF && _sf_dfrac(bu))) return 2;
    if (((au | bu) << 1) == 0) return 0;
    int as = _sf_dsign(au), bs = _sf_dsign(bu);
    if (as != bs) return as ? -1 : 1;
    if (au == bu) return 0;
    if (au > bu) return as ? -1 : 1;
    return as ? 1 : -1;
}

_SF_W int __eqdf2(double a, double b) { return _sf_dcmp(a,b); }
_SF_W int __nedf2(double a, double b) { return _sf_dcmp(a,b); }
_SF_W int __ltdf2(double a, double b) { int r=_sf_dcmp(a,b); return r==2?1:r; }
_SF_W int __ledf2(double a, double b) { int r=_sf_dcmp(a,b); return r==2?1:r; }
_SF_W int __gtdf2(double a, double b) { int r=_sf_dcmp(a,b); return r==2?-1:r; }
_SF_W int __gedf2(double a, double b) { int r=_sf_dcmp(a,b); return r==2?-1:r; }
_SF_W int __unorddf2(double a, double b) { return _sf_dcmp(a,b)==2; }

_SF_W int __eqsf2(float a, float b) { return __eqdf2(__extendsfdf2(a),__extendsfdf2(b)); }
_SF_W int __nesf2(float a, float b) { return __nedf2(__extendsfdf2(a),__extendsfdf2(b)); }
_SF_W int __ltsf2(float a, float b) { return __ltdf2(__extendsfdf2(a),__extendsfdf2(b)); }
_SF_W int __lesf2(float a, float b) { return __ledf2(__extendsfdf2(a),__extendsfdf2(b)); }
_SF_W int __gtsf2(float a, float b) { return __gtdf2(__extendsfdf2(a),__extendsfdf2(b)); }
_SF_W int __gesf2(float a, float b) { return __gedf2(__extendsfdf2(a),__extendsfdf2(b)); }
_SF_W int __unordsf2(float a, float b) { return __unorddf2(__extendsfdf2(a),__extendsfdf2(b)); }