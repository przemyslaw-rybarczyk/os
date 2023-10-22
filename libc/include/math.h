#pragma once

#include <limits.h>

#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

#define HUGE_VALF INFINITY
#define HUGE_VAL ((double)INFINITY)
#define HUGE_VALL ((long double)INFINITY)

#define FP_NORMAL 0
#define FP_SUBNORMAL 1
#define FP_ZERO 2
#define FP_INFINITE 3
#define FP_NAN 4

int __fpclassifyf(float f);
int __fpclassify(double f);
int __fpclassifyl(long double f);

int __signbitf(float f);
int __signbit(double f);
int __signbitl(long double f);

#define fpclassify(x) (_Generic((x), float: __fpclassifyf((x)), double: __fpclassify((x)), long double: __fpclassifyl((x))))
#define isfinite(x) (fpclassify((x)) <= 2)
#define isinf(x) (fpclassify((x)) == FP_INFINITE)
#define isnan(x) (fpclassify((x)) == FP_NAN)
#define isnormal(x) (fpclassify((x)) == FP_NORMAL)
#define signbit(x) (_Generic((x), float: __signbitf((x)), double: __signbit((x)), long double: __signbitl((x))))

float frexpf(float f, int *exp);
double frexp(double f, int *exp);
long double frexpl(long double f, int *exp);

float ldexpf(float f, int exp);
double ldexp(double f, int exp);
long double ldexpl(long double f, int exp);

float scalbnf(float f, int exp);
double scalbn(double f, int exp);
long double scalbnl(long double f, int exp);

float scalblnf(float f, long exp);
double scalbln(double f, long exp);
long double scalblnl(long double f, long exp);

float logbf(float f);
double logb(double f);
long double logbl(long double f);

#define FP_ILOGB0 (-INT_MAX)
#define FP_ILOGBNAN INT_MIN

int ilogbf(float f);
int ilogb(double f);
int ilogbl(long double f);

float nextafterf(float from, float to);
double nextafter(double from, double to);
long double nextafterl(long double from, long double to);

float nexttowardf(float from, long double to);
double nexttoward(double from, long double to);
long double nexttowardl(long double from, long double to);

float copysignf(float x, float y);
double copysign(double x, double y);
long double copysignl(long double x, long double y);

float rintf(float f);
double rint(double f);
long double rintl(long double f);

long lrintf(float f);
long lrint(double f);
long lrintl(long double f);

long long llrintf(float f);
long long llrint(double f);
long long llrintl(long double f);

float nearbyintf(float f);
double nearbyint(double f);
long double nearbyintl(long double f);

float roundf(float f);
double round(double f);
long double roundl(long double f);

long lroundf(float f);
long lround(double f);
long lroundl(long double f);

long long llroundf(float f);
long long llround(double f);
long long llroundl(long double f);

float truncf(float f);
double trunc(double f);
long double truncl(long double f);

float floorf(float f);
double floor(double f);
long double floorl(long double f);

float ceilf(float f);
double ceil(double f);
long double ceill(long double f);

float modff(float f, float *iptr);
double modf(double f, double *iptr);
long double modfl(long double f, long double *iptr);

float fabsf(float f);
double fabs(double f);
long double fabsl(long double f);

float fmodf(float x, float y);
double fmod(double x, double y);
long double fmodl(long double x, long double y);

float remainderf(float x, float y);
double remainder(double x, double y);
long double remainderl(long double x, long double y);

float remquof(float x, float y, int *quo);
double remquo(double x, double y, int *quo);
long double remquol(long double x, long double y, int *quo);

float fmaf(float x, float y, float z);
double fma(double x, double y, double z);
long double fmal(long double x, long double y, long double z);

float fmaxf(float x, float y);
double fmax(double x, double y);
long double fmaxl(long double x, long double y);

float fminf(float x, float y);
double fmin(double x, double y);
long double fminl(long double x, long double y);

float fdimf(float x, float y);
double fdim(double x, double y);
long double fdiml(long double x, long double y);

float nanf(const char *arg);
double nan(const char *arg);
long double nanl(const char *arg);

float exp2f(float f);
double exp2(double f);
long double exp2l(long double f);

float expf(float f);
double exp(double f);
long double expl(long double f);

float expm1f(float f);
double expm1(double f);
long double expm1l(long double f);

float log2f(float f);
double log2(double f);
long double log2l(long double f);

float logf(float f);
double log(double f);
long double logl(long double f);

float log10f(float f);
double log10(double f);
long double log10l(long double f);

float log1pf(float f);
double log1p(double f);
long double log1pl(long double f);

float powf(float x, float y);
double pow(double x, double y);
long double powl(long double x, long double y);

float sqrtf(float f);
double sqrt(double f);
long double sqrtl(long double f);

float cbrtf(float f);
double cbrt(double f);
long double cbrtl(long double f);

float sinf(float f);
double sin(double f);
long double sinl(long double f);

float cosf(float f);
double cos(double f);
long double cosl(long double f);

float tanf(float f);
double tan(double f);
long double tanl(long double f);

float atan2f(float y, float x);
double atan2(double y, double x);
long double atan2l(long double y, long double x);

float atanf(float f);
double atan(double f);
long double atanl(long double f);

float asinf(float f);
double asin(double f);
long double asinl(long double f);

float acosf(float f);
double acos(double f);
long double acosl(long double f);
