#pragma once

#include <stdint.h>

// Used to cast floating-point numbers into their binary representation

union float_cast {
    float f;
    uint32_t n;
};

union double_cast {
    double f;
    uint64_t n;
};

union long_double_cast {
    long double f;
    struct {
        uint64_t mantissa;
        uint16_t sign_exponent;
    } __attribute__((packed));
};

#define FLOAT_MANTISSA_MASK UINT32_C(0x007FFFFF)
#define FLOAT_MANTISSA_BITS 23
#define FLOAT_EXPONENT_MASK UINT32_C(0x7F800000)
#define FLOAT_EXPONENT_MAX UINT32_C(0xFF)
#define FLOAT_EXPONENT_BIAS 127
#define FLOAT_EXPONENT_OFFSET UINT32_C(23)
#define FLOAT_SIGN_MASK UINT32_C(0x80000000)
#define FLOAT_SIGN_OFFSET UINT32_C(31)

#define DOUBLE_MANTISSA_MASK UINT64_C(0x000FFFFFFFFFFFFF)
#define DOUBLE_MANTISSA_BITS 52
#define DOUBLE_EXPONENT_MASK UINT64_C(0x7FF0000000000000)
#define DOUBLE_EXPONENT_MAX UINT64_C(0x7FF)
#define DOUBLE_EXPONENT_BIAS UINT64_C(1023)
#define DOUBLE_EXPONENT_OFFSET UINT64_C(52)
#define DOUBLE_SIGN_MASK UINT64_C(0x8000000000000000)
#define DOUBLE_SIGN_OFFSET UINT64_C(63)

#define LONG_DOUBLE_MANTISSA_MASK UINT64_C(0x7FFFFFFFFFFFFFFF)
#define LONG_DOUBLE_MANTISSA_BITS 63
#define LONG_DOUBLE_EXPONENT_MASK UINT32_C(0x7FFF)
#define LONG_DOUBLE_EXPONENT_MAX UINT32_C(0x7FFF)
#define LONG_DOUBLE_EXPONENT_BIAS UINT32_C(16383)
#define LONG_DOUBLE_SIGN_MASK UINT32_C(0x8000)
#define LONG_DOUBLE_SIGN_OFFSET 15
