/*
 * complex.c — POSIX complex math function implementations.
 *
 * Implements all functions declared in <complex.h>.
 *
 * Access to real/imaginary parts uses GCC's __real__/__imag__ operators,
 * which compile to simple struct-field loads/stores with no runtime cost.
 * All arithmetic delegates to the corresponding real functions in math.c
 * (sin, cos, exp, log, sqrt, hypot, atan2) via Euler's formula and standard
 * complex identities.
 *
 * rv64imac note: floating-point operations are implemented in software by
 * libgcc; these functions are correct for any IEEE 754 double precision
 * implementation regardless of hardware.
 */

#include "complex.h"
#include "math.h"

/* ── Accessors: creal / cimag ────────────────────────────────────────────── */

double      creal(double _Complex z)       { return __real__ z; }
float       crealf(float _Complex z)       { return __real__ z; }
long double creall(long double _Complex z) { return __real__ z; }

double      cimag(double _Complex z)       { return __imag__ z; }
float       cimagf(float _Complex z)       { return __imag__ z; }
long double cimagl(long double _Complex z) { return __imag__ z; }

/* ── Conjugate ───────────────────────────────────────────────────────────── */

double _Complex conj(double _Complex z) {
    double _Complex r;
    __real__ r =  __real__ z;
    __imag__ r = -__imag__ z;
    return r;
}
float _Complex conjf(float _Complex z) {
    float _Complex r;
    __real__ r =  __real__ z;
    __imag__ r = -__imag__ z;
    return r;
}
long double _Complex conjl(long double _Complex z) {
    long double _Complex r;
    __real__ r =  __real__ z;
    __imag__ r = -__imag__ z;
    return r;
}

/* ── Modulus: cabs(z) = sqrt(Re²+Im²) ───────────────────────────────────── */

double      cabs(double _Complex z)       { return hypot(__real__ z, __imag__ z); }
float       cabsf(float _Complex z)       { return hypotf(__real__ z, __imag__ z); }
long double cabsl(long double _Complex z) { return hypotl(__real__ z, __imag__ z); }

/* ── Argument: carg(z) = atan2(Im, Re) ──────────────────────────────────── */

double      carg(double _Complex z)       { return atan2(__imag__ z, __real__ z); }
float       cargf(float _Complex z)       { return atan2f(__imag__ z, __real__ z); }
long double cargl(long double _Complex z) { return atan2l(__imag__ z, __real__ z); }

/* ── Projection onto Riemann sphere ─────────────────────────────────────── */

double _Complex cproj(double _Complex z) {
    if (!isinf(__real__ z) && !isinf(__imag__ z)) return z;
    double _Complex r;
    __real__ r = __builtin_inf();
    __imag__ r = copysign(0.0, __imag__ z);
    return r;
}
float _Complex cprojf(float _Complex z) {
    if (!isinf(__real__ z) && !isinf(__imag__ z)) return z;
    float _Complex r;
    __real__ r = __builtin_inff();
    __imag__ r = copysignf(0.0f, __imag__ z);
    return r;
}
long double _Complex cprojl(long double _Complex z) {
    if (!isinf((double)__real__ z) && !isinf((double)__imag__ z)) return z;
    long double _Complex r;
    __real__ r = __builtin_infl();
    __imag__ r = copysignl(0.0L, __imag__ z);
    return r;
}

/* ── Square root: csqrt(z) = sqrt((|z|+Re)/2) + i*sign(Im)*sqrt((|z|-Re)/2) */

double _Complex csqrt(double _Complex z) {
    double r = cabs(z), x = __real__ z, y = __imag__ z;
    double _Complex res;
    __real__ res = sqrt((r + x) * 0.5);
    __imag__ res = (y < 0.0 ? -1.0 : 1.0) * sqrt((r - x) * 0.5);
    return res;
}
float _Complex csqrtf(float _Complex z) {
    float r = cabsf(z), x = __real__ z, y = __imag__ z;
    float _Complex res;
    __real__ res = sqrtf((r + x) * 0.5f);
    __imag__ res = (y < 0.0f ? -1.0f : 1.0f) * sqrtf((r - x) * 0.5f);
    return res;
}
long double _Complex csqrtl(long double _Complex z) {
    return (long double _Complex)csqrt((double _Complex)z);
}

/* ── Exponential: cexp(z) = e^Re * (cos(Im) + i*sin(Im)) ───────────────── */

double _Complex cexp(double _Complex z) {
    double er = exp(__real__ z);
    double _Complex res;
    __real__ res = er * cos(__imag__ z);
    __imag__ res = er * sin(__imag__ z);
    return res;
}
float _Complex cexpf(float _Complex z) {
    float er = expf(__real__ z);
    float _Complex res;
    __real__ res = er * cosf(__imag__ z);
    __imag__ res = er * sinf(__imag__ z);
    return res;
}
long double _Complex cexpl(long double _Complex z) {
    return (long double _Complex)cexp((double _Complex)z);
}

/* ── Logarithm: clog(z) = log|z| + i*arg(z) ─────────────────────────────── */

double _Complex clog(double _Complex z) {
    double _Complex res;
    __real__ res = log(cabs(z));
    __imag__ res = carg(z);
    return res;
}
float _Complex clogf(float _Complex z) {
    float _Complex res;
    __real__ res = logf(cabsf(z));
    __imag__ res = cargf(z);
    return res;
}
long double _Complex clogl(long double _Complex z) {
    return (long double _Complex)clog((double _Complex)z);
}

/* ── Power: cpow(z,w) = cexp(clog(z) * w) ───────────────────────────────── */

double _Complex cpow(double _Complex z, double _Complex w) {
    return cexp(clog(z) * w);
}
float _Complex cpowf(float _Complex z, float _Complex w) {
    return cexpf(clogf(z) * w);
}
long double _Complex cpowl(long double _Complex z, long double _Complex w) {
    return (long double _Complex)cpow((double _Complex)z, (double _Complex)w);
}

/* ── Trigonometric functions ────────────────────────────────────────────── */

/* csin(z) = sin(Re)*cosh(Im) + i*cos(Re)*sinh(Im) */
double _Complex csin(double _Complex z) {
    double _Complex res;
    __real__ res = sin(__real__ z) * cosh(__imag__ z);
    __imag__ res = cos(__real__ z) * sinh(__imag__ z);
    return res;
}
float _Complex csinf(float _Complex z) {
    float _Complex res;
    __real__ res = sinf(__real__ z) * coshf(__imag__ z);
    __imag__ res = cosf(__real__ z) * sinhf(__imag__ z);
    return res;
}
long double _Complex csinl(long double _Complex z) {
    return (long double _Complex)csin((double _Complex)z);
}

/* ccos(z) = cos(Re)*cosh(Im) - i*sin(Re)*sinh(Im) */
double _Complex ccos(double _Complex z) {
    double _Complex res;
    __real__ res =  cos(__real__ z) * cosh(__imag__ z);
    __imag__ res = -sin(__real__ z) * sinh(__imag__ z);
    return res;
}
float _Complex ccosf(float _Complex z) {
    float _Complex res;
    __real__ res =  cosf(__real__ z) * coshf(__imag__ z);
    __imag__ res = -sinf(__real__ z) * sinhf(__imag__ z);
    return res;
}
long double _Complex ccosl(long double _Complex z) {
    return (long double _Complex)ccos((double _Complex)z);
}

/* ctan(z) = csin(z) / ccos(z) */
double _Complex ctan(double _Complex z) { return csin(z) / ccos(z); }
float  _Complex ctanf(float _Complex z) { return csinf(z) / ccosf(z); }
long double _Complex ctanl(long double _Complex z) {
    return (long double _Complex)ctan((double _Complex)z);
}

/* ── Inverse trigonometric ───────────────────────────────────────────────── */

/* casin(z) = -i * clog(i*z + csqrt(1 - z*z)) */
double _Complex casin(double _Complex z) {
    double _Complex iz, one = 1.0 + 0.0 * I;
    __real__ iz = -__imag__ z; __imag__ iz = __real__ z;
    double _Complex w = clog(iz + csqrt(one - z * z));
    double _Complex res; __real__ res = __imag__ w; __imag__ res = -__real__ w;
    return res;
}
float _Complex casinf(float _Complex z) { return (float _Complex)casin((double _Complex)z); }
long double _Complex casinl(long double _Complex z) { return (long double _Complex)casin((double _Complex)z); }

/* cacos(z) = pi/2 - casin(z) */
double _Complex cacos(double _Complex z) {
    double _Complex s = casin(z);
    double _Complex res;
    __real__ res = M_PI_2 - __real__ s;
    __imag__ res = -__imag__ s;
    return res;
}
float _Complex cacosf(float _Complex z) { return (float _Complex)cacos((double _Complex)z); }
long double _Complex cacosl(long double _Complex z) { return (long double _Complex)cacos((double _Complex)z); }

/* catan(z) = (i/2) * (clog(1 - i*z) - clog(1 + i*z)) */
double _Complex catan(double _Complex z) {
    double _Complex one = 1.0 + 0.0 * I;
    double _Complex iz; __real__ iz = -__imag__ z; __imag__ iz = __real__ z;
    double _Complex w = clog(one - iz) - clog(one + iz);
    double _Complex res;
    __real__ res = -__imag__ w * 0.5;
    __imag__ res =  __real__ w * 0.5;
    return res;
}
float _Complex catanf(float _Complex z) { return (float _Complex)catan((double _Complex)z); }
long double _Complex catanl(long double _Complex z) { return (long double _Complex)catan((double _Complex)z); }

/* ── Hyperbolic functions ────────────────────────────────────────────────── */

/* csinh(z) = sinh(Re)*cos(Im) + i*cosh(Re)*sin(Im) */
double _Complex csinh(double _Complex z) {
    double _Complex res;
    __real__ res = sinh(__real__ z) * cos(__imag__ z);
    __imag__ res = cosh(__real__ z) * sin(__imag__ z);
    return res;
}
float _Complex csinhf(float _Complex z) { return (float _Complex)csinh((double _Complex)z); }
long double _Complex csinhl(long double _Complex z) { return (long double _Complex)csinh((double _Complex)z); }

/* ccosh(z) = cosh(Re)*cos(Im) + i*sinh(Re)*sin(Im) */
double _Complex ccosh(double _Complex z) {
    double _Complex res;
    __real__ res = cosh(__real__ z) * cos(__imag__ z);
    __imag__ res = sinh(__real__ z) * sin(__imag__ z);
    return res;
}
float _Complex ccoshf(float _Complex z) { return (float _Complex)ccosh((double _Complex)z); }
long double _Complex ccoshl(long double _Complex z) { return (long double _Complex)ccosh((double _Complex)z); }

/* ctanh(z) = csinh(z) / ccosh(z) */
double _Complex ctanh(double _Complex z) { return csinh(z) / ccosh(z); }
float  _Complex ctanhf(float _Complex z) { return csinhf(z) / ccoshf(z); }
long double _Complex ctanhl(long double _Complex z) { return (long double _Complex)ctanh((double _Complex)z); }

/* ── Inverse hyperbolic ──────────────────────────────────────────────────── */

/* casinh(z) = clog(z + csqrt(z*z + 1)) */
double _Complex casinh(double _Complex z) {
    double _Complex one = 1.0 + 0.0 * I;
    return clog(z + csqrt(z * z + one));
}
float _Complex casinhf(float _Complex z) { return (float _Complex)casinh((double _Complex)z); }
long double _Complex casinhl(long double _Complex z) { return (long double _Complex)casinh((double _Complex)z); }

/* cacosh(z) = clog(z + csqrt(z*z - 1)) */
double _Complex cacosh(double _Complex z) {
    double _Complex one = 1.0 + 0.0 * I;
    return clog(z + csqrt(z * z - one));
}
float _Complex cacoshf(float _Complex z) { return (float _Complex)cacosh((double _Complex)z); }
long double _Complex cacoshl(long double _Complex z) { return (long double _Complex)cacosh((double _Complex)z); }

/* catanh(z) = (1/2) * (clog(1+z) - clog(1-z)) */
double _Complex catanh(double _Complex z) {
    double _Complex one = 1.0 + 0.0 * I;
    return (clog(one + z) - clog(one - z)) * 0.5;
}
float _Complex catanhf(float _Complex z) { return (float _Complex)catanh((double _Complex)z); }
long double _Complex catanhl(long double _Complex z) { return (long double _Complex)catanh((double _Complex)z); }
