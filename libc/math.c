#include <math.h>

#include <stdbool.h>

#include "float_cast.h"

int __fpclassifyf(float f) {
    uint32_t n = (union float_cast){.f = f}.n;
    uint32_t mantissa = n & FLOAT_MANTISSA_MASK;
    uint32_t exponent = (n & FLOAT_EXPONENT_MASK) >> FLOAT_EXPONENT_OFFSET;
    if (exponent == 0)
        return mantissa == 0 ? FP_ZERO : FP_SUBNORMAL;
    else if (exponent == FLOAT_EXPONENT_MAX)
        return mantissa == 0 ? FP_INFINITE : FP_NAN;
    else
        return FP_NORMAL;
}

int __fpclassify(double f) {
    uint64_t n = (union double_cast){.f = f}.n;
    uint64_t mantissa = n & DOUBLE_MANTISSA_MASK;
    uint64_t exponent = (n & DOUBLE_EXPONENT_MASK) >> DOUBLE_EXPONENT_OFFSET;
    if (exponent == 0)
        return mantissa == 0 ? FP_ZERO : FP_SUBNORMAL;
    else if (exponent == DOUBLE_EXPONENT_MAX)
        return mantissa == 0 ? FP_INFINITE : FP_NAN;
    else
        return FP_NORMAL;
}

int __fpclassifyl(long double f) {
    union long_double_cast ld_cast = (union long_double_cast){.f = f};
    uint64_t mantissa = ld_cast.mantissa & LONG_DOUBLE_MANTISSA_MASK;
    uint16_t exponent = ld_cast.sign_exponent & LONG_DOUBLE_EXPONENT_MASK;
    if (exponent == 0)
        return mantissa == 0 ? FP_ZERO : FP_SUBNORMAL;
    else if (exponent == LONG_DOUBLE_EXPONENT_MAX)
        return mantissa == 0 ? FP_INFINITE : FP_NAN;
    else
        return FP_NORMAL;
}

int __signbitf(float f) {
    uint32_t n = (union float_cast){.f = f}.n;
    return n >> FLOAT_SIGN_OFFSET;
}

int __signbit(double f) {
    uint64_t n = (union double_cast){.f = f}.n;
    return n >> DOUBLE_SIGN_OFFSET;
}

int __signbitl(long double f) {
    union long_double_cast ld_cast = (union long_double_cast){.f = f};
    return ld_cast.sign_exponent >> LONG_DOUBLE_SIGN_OFFSET;
}

float frexpf(float f, int *exp) {
    return frexpl(f, exp);
}

double frexp(double f, int *exp) {
    return frexpl(f, exp);
}

long double __frexpl(long double f, int *exp);

long double frexpl(long double f, int *exp) {
    switch (fpclassify(f)) {
    case FP_ZERO:
    case FP_INFINITE:
    case FP_NAN:
        *exp = 0;
        return f;
    default:
        return __frexpl(f, exp);
    }
}

float ldexpf(float f, int exp) {
    return scalblnl(f, exp);
}

double ldexp(double f, int exp) {
    return scalblnl(f, exp);
}

long double ldexpl(long double f, int exp) {
    return scalblnl(f, exp);
}

float scalbnf(float f, int exp) {
    return scalblnl(f, exp);
}

double scalbn(double f, int exp) {
    return scalblnl(f, exp);
}

long double scalbnl(long double f, int exp) {
    return scalblnl(f, exp);
}

float scalblnf(float f, long exp) {
    return scalblnl(f, exp);
}

double scalbln(double f, long exp) {
    return scalblnl(f, exp);
}

float logbf(float f) {
    return logbl(f);
}

double logb(double f) {
    return logbl(f);
}

int ilogbf(float f) {
    return ilogbl(f);
}

int ilogb(double f) {
    return ilogbl(f);
}

int __ilogbl(long double f);

int ilogbl(long double f) {
    switch (fpclassify(f)) {
    case FP_ZERO:
        return FP_ILOGB0;
    case FP_INFINITE:
        return INT_MAX;
    case FP_NAN:
        return FP_ILOGBNAN;
    default:
        return __ilogbl(f);
    }
}

float nextafterf(float from, float to) {
    return nexttowardf(from, to);
}

double nextafter(double from, double to) {
    return nexttoward(from, to);
}

long double nextafterl(long double from, long double to) {
    return nexttowardl(from, to);
}

float nexttowardf(float from, long double to) {
    // Handle equality and nan
    if (from == to || isnan(to))
        return to;
    if (isnan(from))
        return from;
    uint32_t n = (union float_cast){.f = from}.n;
    if (from == 0.0) {
        // If `from` is zero, return the smallest subnormal in the given direction
        n = (to < 0.0 ? (UINT64_C(1) << FLOAT_SIGN_OFFSET) : 0) | 1;
    } else {
        // Add or subtract one depending on whether we're moving to or away from zero
        // This works because the overflow from the mantissa will carry over to the exponent.
        bool towards_zero = (n >> FLOAT_SIGN_OFFSET) == (from < to);
        n += towards_zero ? -1 : 1;
    }
    return (union float_cast){.n = n}.f;
}

double nexttoward(double from, long double to) {
    // Handle equality and nan
    if (from == to || isnan(to))
        return to;
    if (isnan(from))
        return from;
    uint64_t n = (union double_cast){.f = from}.n;
    if (from == 0.0) {
        // If `from` is zero, return the smallest subnormal in the given direction
        n = (to < 0.0 ? (UINT64_C(1) << DOUBLE_SIGN_OFFSET) : 0) | 1;
    } else {
        // Add or subtract one depending on whether we're moving to or away from zero
        // This works because the overflow from the mantissa will carry over to the exponent.
        bool towards_zero = (n >> DOUBLE_SIGN_OFFSET) == (from < to);
        n += towards_zero ? -1 : 1;
    }
    return (union double_cast){.n = n}.f;
}

long double nexttowardl(long double from, long double to) {
    // Handle equality and nan
    if (from == to || isnan(to))
        return to;
    if (isnan(from))
        return from;
    union long_double_cast ld_cast = (union long_double_cast){.f = from};
    if (from == 0.0) {
        // If `from` is zero, return the smallest subnormal in the given direction
        ld_cast.mantissa = 1;
        ld_cast.sign_exponent = to < 0.0 ? (UINT64_C(1) << LONG_DOUBLE_SIGN_OFFSET) : 0;
    } else {
        // Add or subtract one to the mantissa depending on whether we're moving to or away from zero
        uint16_t exponent = ld_cast.sign_exponent & LONG_DOUBLE_EXPONENT_MASK;
        bool towards_zero = (ld_cast.sign_exponent >> LONG_DOUBLE_SIGN_OFFSET) == (from < to);
        uint64_t mantissa = (ld_cast.mantissa & LONG_DOUBLE_MANTISSA_MASK) + (towards_zero ? -1 : 1);
        // If the mantissa overflows, add or subtract one to the exponent
        if (mantissa & (UINT64_C(1) << LONG_DOUBLE_MANTISSA_BITS)) {
            ld_cast.mantissa = mantissa;
            ld_cast.sign_exponent += towards_zero ? -1 : 1;
        } else {
            ld_cast.mantissa = (exponent == 0 ? 0 : (UINT64_C(1) << LONG_DOUBLE_MANTISSA_BITS)) | mantissa;
        }
    }
    return ld_cast.f;
}

float copysignf(float x, float y) {
    uint32_t nx = (union float_cast){.f = x}.n;
    uint32_t ny = (union float_cast){.f = y}.n;
    return (union float_cast){.n = (ny & FLOAT_SIGN_MASK) | (nx & ~FLOAT_SIGN_MASK)}.f;
}

double copysign(double x, double y) {
    uint64_t nx = (union double_cast){.f = x}.n;
    uint64_t ny = (union double_cast){.f = y}.n;
    return (union double_cast){.n = (ny & DOUBLE_SIGN_MASK) | (nx & ~DOUBLE_SIGN_MASK)}.f;
}

long double copysignl(long double x, long double y) {
    union long_double_cast ld_cast_x = (union long_double_cast){.f = x};
    union long_double_cast ld_cast_y = (union long_double_cast){.f = y};
    ld_cast_x.sign_exponent = (ld_cast_y.sign_exponent & LONG_DOUBLE_SIGN_MASK) | (ld_cast_x.sign_exponent & ~LONG_DOUBLE_SIGN_MASK);
    return ld_cast_x.f;
}

float rintf(float f) {
    return rintl(f);
}

double rint(double f) {
    return rintl(f);
}

float nearbyintf(float f) {
    return rintl(f);
}

double nearbyint(double f) {
    return rintl(f);
}

long double nearbyintl(long double f) {
    return rintl(f);
}

float roundf(float f) {
    return roundl(f);
}

double round(double f) {
    return roundl(f);
}

float truncf(float f) {
    return truncl(f);
}

double trunc(double f) {
    return truncl(f);
}

float floorf(float f) {
    return floorl(f);
}

double floor(double f) {
    return floorl(f);
}

float ceilf(float f) {
    return ceill(f);
}

double ceil(double f) {
    return ceill(f);
}

float modff(float f, float *iptr) {
    *iptr = truncf(f);
    if (isinf(f) || f == 0.0)
        return f;
    return f - truncf(f);
}

double modf(double f, double *iptr) {
    *iptr = trunc(f);
    if (isinf(f) || f == 0.0)
        return f;
    return f - trunc(f);
}

long double modfl(long double f, long double *iptr) {
    *iptr = truncl(f);
    if (isinf(f) || f == 0.0)
        return f;
    return f - truncl(f);
}

float fabsf(float f) {
    uint32_t n = (union float_cast){.f = f}.n;
    return (union float_cast){.n = n & ~FLOAT_SIGN_MASK}.f;
}

double fabs(double f) {
    uint64_t n = (union double_cast){.f = f}.n;
    return (union double_cast){.n = n & ~DOUBLE_SIGN_MASK}.f;
}

long double fabsl(long double f) {
    union long_double_cast ld_cast = (union long_double_cast){.f = f};
    ld_cast.sign_exponent &= ~LONG_DOUBLE_SIGN_MASK;
    return ld_cast.f;
}

float fmodf(float x, float y) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y)))
        return x;
    return x - truncf(x / y) * y;
}

double fmod(double x, double y) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y)))
        return x;
    return x - trunc(x / y) * y;
}

long double fmodl(long double x, long double y) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y)))
        return x;
    return x - truncl(x / y) * y;
}

float remainderf(float x, float y) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y)))
        return x;
    return x - roundf(x / y) * y;
}

double remainder(double x, double y) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y)))
        return x;
    return x - round(x / y) * y;
}

long double remainderl(long double x, long double y) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y)))
        return x;
    return x - roundl(x / y) * y;
}

float remquof(float x, float y, int *quo) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y))) {
        *quo = 0;
        return x;
    }
    float quotient = roundf(x / y);
    *quo = (int)fmodf(quotient, 0x1p31);
    return x - quotient * y;
}

double remquo(double x, double y, int *quo) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y))) {
        *quo = 0;
        return x;
    }
    double quotient = round(x / y);
    *quo = (int)fmod(quotient, 0x1p31);
    return x - quotient * y;
}

long double remquol(long double x, long double y, int *quo) {
    if ((x == 0.0 && isfinite(y) && y != 0.0) || (isfinite(x) && isinf(y))) {
        *quo = 0;
        return x;
    }
    long double quotient = roundl(x / y);
    *quo = (int)fmodl(quotient, 0x1p31);
    return x - quotient * y;
}

float fmaf(float x, float y, float z) {
    return x * y + z;
}

double fma(double x, double y, double z) {
    return x * y + z;
}

long double fmal(long double x, long double y, long double z) {
    return x * y + z;
}

float fmaxf(float x, float y) {
    return (x > y || isnan(y)) ? x : y;
}

double fmax(double x, double y) {
    return (x > y || isnan(y)) ? x : y;
}

long double fmaxl(long double x, long double y) {
    return (x > y || isnan(y)) ? x : y;
}

float fminf(float x, float y) {
    return (x < y || isnan(y)) ? x : y;
}

double fmin(double x, double y) {
    return (x < y || isnan(y)) ? x : y;
}

long double fminl(long double x, long double y) {
    return (x < y || isnan(y)) ? x : y;
}

float fdimf(float x, float y) {
    return x - y <= 0 ? 0 : x - y;
}

double fdim(double x, double y) {
    return x - y <= 0 ? 0 : x - y;
}

long double fdiml(long double x, long double y) {
    return x - y <= 0 ? 0 : x - y;
}

float nanf(const char *arg __attribute__((unused))) {
    return NAN;
}

double nan(const char *arg __attribute__((unused))) {
    return NAN;
}

long double nanl(const char *arg __attribute__((unused))) {
    return NAN;
}

float exp2f(float f) {
    return exp2l(f);
}

double exp2(double f) {
    return exp2l(f);
}

long double __exp2l(long double f);

long double exp2l(long double f) {
    if (f == INFINITY)
        return INFINITY;
    if (f == -INFINITY)
        return 0;
    return __exp2l(f);
}

float expf(float f) {
    return expl(f);
}

double exp(double f) {
    return expl(f);
}

float expm1f(float f) {
    return expm1l(f);
}

double expm1(double f) {
    return expm1l(f);
}

long double __expm1l(long double f);

long double expm1l(long double f) {
    if (f == INFINITY)
        return INFINITY;
    if (f == -INFINITY)
        return -1;
    return __expm1l(f);
}

float log2f(float f) {
    return log2l(f);
}

double log2(double f) {
    return log2l(f);
}

float logf(float f) {
    return logl(f);
}

double log(double f) {
    return logl(f);
}

float log10f(float f) {
    return log10l(f);
}

double log10(double f) {
    return log10l(f);
}

float log1pf(float f) {
    return log1pl(f);
}

double log1p(double f) {
    return log1pl(f);
}

float powf(float x, float y) {
    return powl(x, y);
}

double pow(double x, double y) {
    return powl(x, y);
}

long double __powl(long double x, long double y);

long double powl(long double x, long double y) {
    if (x == 1.0 || y == 0.0) {
        // Powers of one and zeroth powers are always one.
        // This holds even if the other argument is NaN, so we handle this possibility first.
        return 1.0;
    } else if (isnan(x) || isnan(y)) {
        // If either argument is NaN, return NaN
        return x + y;
    } else if (isinf(y)) {
        // Raising a number to +inf returns +0, 1, or +inf depending on whether its magnitude
        // is less than, equal to, or greater than 1.
        // For -inf, +0 and +inf are reversed.
        long double xp = fabsl(x);
        if (xp == 1.0)
            return 1.0;
        return (xp < 1.0) == (y > 0.0) ? 0.0 : INFINITY;
    } else if (x == 0.0) {
        // Raising ±0 returns +0 for positive exponents and +inf for negative exponents,
        // with the exception of raising -0 to an odd power, which flips the sign.
        if (signbit(x) && fabsl(fmodl(y, 2.0)) == 1.0)
            return y > 0.0 ? -0.0 : -INFINITY;
        else
            return y > 0.0 ? 0.0 : INFINITY;
    } else if (isinf(x)) {
        // Raising ±inf returns +inf for positive exponents and +0 for negative exponents,
        // with the exception of raising -inf to an odd power, which flips the sign.
        if (x < 0.0 && fabsl(fmodl(y, 2.0)) == 1.0)
            return y > 0.0 ? -INFINITY : -0.0;
        else
            return y > 0.0 ? INFINITY : 0.0;
    } else if (x < 0.0) {
        // Exponentiating a negative number is only valid with an integer exponent.
        // We calculate the exponent for the opposite base and negate if the exponent is odd.
        // For non-integer exponents we return NaN.
        long double m = fabsl(fmodl(y, 2.0));
        if (m == 0.0)
            return __powl(-x, y);
        else if (m == 1.0)
            return -__powl(-x, y);
        else
            return NAN;
    } else {
        // We are left with the case where both arguments are finite and the base is positive.
        return __powl(x, y);
    }
}

float sqrtf(float f) {
    return sqrtl(f);
}

double sqrt(double f) {
    return sqrtl(f);
}

float cbrtf(float f) {
    return cbrtl(f);
}

double cbrt(double f) {
    return cbrtl(f);
}

long double cbrtl(long double f) {
    return copysignl(powl(fabsl(f), 1.0l / 3.0l), f);
}

float sinf(float f) {
    return sinl(f);
}

double sin(double f) {
    return sinl(f);
}

long double sinl(long double f) {
    if (isinf(f))
        return NAN;
    if (fabs(f) >= 0x1p63)
        return 0.0;
    asm ("fsin" : "=t"(f) : "t"(f));
    return f;
}

float cosf(float f) {
    return cosl(f);
}

double cos(double f) {
    return cosl(f);
}

long double cosl(long double f) {
    if (isinf(f))
        return NAN;
    if (fabs(f) >= 0x1p63)
        return 0.0;
    asm ("fcos" : "=t"(f) : "t"(f));
    return f;
}

float tanf(float f) {
    return tanl(f);
}

double tan(double f) {
    return tanl(f);
}

long double tanl(long double f) {
    if (isinf(f))
        return NAN;
    if (fabs(f) >= 0x1p63)
        return 0.0;
    asm ("fptan; fstp st(0)" : "=t"(f) : "t"(f));
    return f;
}

float atan2f(float y, float x) {
    return atan2l(y, x);
}

double atan2(double y, double x) {
    return atan2l(y, x);
}

float atanf(float f) {
    return atan2l(f, 1.0);
}

double atan(double f) {
    return atan2l(f, 1.0);
}

long double atanl(long double f) {
    return atan2l(f, 1.0);
}

float asinf(float f) {
    return asinl(f);
}

double asin(double f) {
    return asinl(f);
}

long double asinl(long double f) {
    return atanl(f / sqrt(1.0 - f * f));
}

float acosf(float f) {
    return acosl(f);
}

double acos(double f) {
    return acosl(f);
}

#define PI 0xc.90fdaa22168c235p-2l

long double acosl(long double f) {
    return atanl(sqrt(1.0 - f * f) / f) + (signbit(f) ? PI : 0.0);
}
