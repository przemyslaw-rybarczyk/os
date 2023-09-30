#include <zr/types.h>
#include <stdio.h>

#include <float.h>
#include <stdarg.h>
#include <string.h>

#include <zr/syscalls.h>

bool has_stdout = false;
handle_t stdout_channel;

void _stdio_init(void) {
    err_t err;
    err = resource_get(&resource_name("text/stdout"), RESOURCE_TYPE_CHANNEL_SEND, &stdout_channel);
    if (!err)
        has_stdout = true;
}

int putchar(int c) {
    err_t err;
    if (!has_stdout)
        goto fail;
    unsigned char c_ = (unsigned char)c;
    err = channel_call(stdout_channel, &(SendMessage){1, &(SendMessageData){1, &c_}, 0, NULL}, NULL);
    if (err)
        goto fail;
    return c;
fail:
    return EOF;
}

int puts(const char *s) {
    err_t err;
    if (!has_stdout)
        goto fail;
    err = channel_call(stdout_channel, &(SendMessage){1, &(SendMessageData){strlen(s), s}, 0, NULL}, NULL);
    if (err)
        goto fail;
    return 0;
fail:
    return EOF;
}

// Since the printf() family of functions can print to either a file or a buffer,
// we use a common format that can represent either one.
// `offset` is a pointer to the number of bytes that would be written ignoring buffer size and I/O errors.
// It is used for both file and buffer targets, since its final value is return from the function.
// Since there is currently only one possible target file (stdout), a target with `to_file` set always represents stdout.
// `buffer` and `size` are used only for buffer targets.
struct printf_target {
    bool to_file;
    size_t *offset;
    char *restrict buffer;
    size_t size;
};

// Print a single character to a printf target
static void printf_char(struct printf_target target, char c) {
    if (target.to_file) {
        // Print to stdout
        putchar(c);
    } else {
        // Write a byte to a buffer if it's within bounds
        if (*(target.offset) < target.size) {
            target.buffer[*(target.offset)] = c;
        }
    }
    (*(target.offset))++;
}

// Print a null-terminated string
static void printf_string(struct printf_target target, const char *s) {
    for (const char *p = s; *p != '\0'; p++)
        printf_char(target, *p);
}

// Print an unsinged decimal number
static void printf_dec(struct printf_target target, uintmax_t n) {
    if (n == 0) {
        printf_char(target, '0');
        return;
    }
    // Since the digits will be generated in the reverse order from the one we need, we put them in a buffer before printing them.
    // 2.41 is a little above the decimal logarithm of 256 (the number of decimal digits a byte contains).
    char digits[(size_t)(sizeof(uintmax_t) * 2.41 + 1)];
    int i = 0;
    // Generate the digits
    for (; n > 0; n /= 10)
        digits[i++] = (n % 10) + '0';
    // Print the digits in reverse order
    for (int j = 0; j < i; j++)
        printf_char(target, digits[i - j - 1]);
}

// Print a singed decimal number
static void printf_dec_signed(struct printf_target target, intmax_t n) {
    uintmax_t nu; // Absolute value of n
    // If the number is negative, print a minus sign and invert it
    if (n < 0) {
        printf_char(target, '-');
        nu = -n;
    } else {
        nu = n;
    }
    // Print the number without the sign
    printf_dec(target, nu);
}

// Print an unsinged octal number
static void printf_oct(struct printf_target target, uintmax_t n) {
    if (n == 0) {
        printf_char(target, '0');
        return;
    }
    // Index of octal digit
    int i = (8 * sizeof(uintmax_t) - 1) / 3;
    // Skip initial zeroes
    for (; i >= 0 && ((n >> (3 * i)) & 0x7) == 0; i--)
        ;
    // Print digits
    for (; i >= 0; i--) {
        int digit = (n >> (3 * i)) & 0x7;
        printf_char(target, digit + '0');
    }
}

// Print an unsinged hexadecimal number
// `uppercase` determines whether digits above 9 are printed as "ABCDEF" or "abcdef".
static void printf_hex(struct printf_target target, uintmax_t n, bool uppercase) {
    if (n == 0) {
        printf_char(target, '0');
        return;
    }
    // Index of hexadecimal digit
    int i = 2 * sizeof(uintmax_t) - 1;
    // Skip initial zeroes
    for (; i >= 0 && ((n >> (4 * i)) & 0xF) == 0; i--)
        ;
    // Print digits
    for (; i >= 0; i--) {
        int digit = (n >> (4 * i)) & 0xF;
        printf_char(target, digit < 10 ? digit + '0' : digit - 10 + (uppercase ? 'A' : 'a'));
    }
}

// Used to cast a long double into its components
union long_double_cast {
    long double ld;
    struct {
        u64 mantissa;
        u16 sign_exponent;
    } __attribute__((packed));
};

// Verify that long double is 80-bit extended precision format
// The code for printing floating point numbers relies on this assumption.
#if !(FLT_RADIX == 2 && LDBL_MANT_DIG == 64 && LDBL_MIN_EXP == -16381 && LDBL_MAX_EXP == 16384)
#error "long double is not 80-bit extended precision format"
#endif

// 10^19 - the largest power of 10 that can fit in a u64
#define POW_10_19 UINT64_C(10000000000000000000)

// Print a floating-point number
// `uppercase` determines whether nan and inf are printed in lowercase or uppercase.
// The conversion algorithm used is very basic and does not properly round the result, but only truncates it.
static void printf_float(struct printf_target target, long double f, bool uppercase) {
    // Extract mantissa and exponent fields
    union long_double_cast f_cast;
    f_cast.ld = f;
    u64 mantissa = f_cast.mantissa;
    u16 exponent_field = f_cast.sign_exponent & 0x7FFF;
    // If the sign bit is set, print a minus sign
    if (f_cast.sign_exponent & 0x8000)
        printf_char(target, '-');
    // Handle NaN and infinity
    if (exponent_field == 0x7FFF) {
        // When checking the mantissa, ignore the highest bit, since it should be 1
        if ((mantissa & 0x7FFFFFFFFFFFFFFF) == 0)
            printf_string(target, uppercase ? "INF" : "inf");
        else
            printf_string(target, uppercase ? "NAN" : "nan");
        return;
    }
    // Subtract the bias from the exponent
    i32 exponent = exponent_field - 16383;
    // If the number is denormal, adjust the exponent to account for it
    // There is no need to change anything else, since the integral part of the mantissa is already explicitly stored in this format.
    if (exponent_field == 0) {
        exponent += 1;
    }
    // Buffer for storing the expanded form of the integral part or the fractional part of the floating-point number
    // Must be long enough to hold -(maximum exponent) + (length of mantissa) - 1 bits = (16383 + 64 - 1) / 8 qwords = 256.96875 qwords
    u64 fp_digits[257];
    int fp_digits_num;
    // Buffer for storing the groups of digits created when converting the integral part to decimal
    // Each element represents 19 decimal digits.
    // Must be long enough to hold log (2^16383) base (10^19) elements ≈ 259.56707 elements
    u64 dec_digit_groups[260];
    int dec_digit_groups_num;
    // Buffer for storing the digits created when converting a digit group to its individual digits
    char dec_digits[19];
    // Convert the integral part to a little-endian big integer
    // This involves shifting the mantissa left by the exponent
    fp_digits_num = exponent / 64 + 1;
    if (exponent < 0) {
        // The mantissa is entirely within the fractional part of the number, so the integral part is zero
        fp_digits_num = 0;
    } else if (exponent < 64) {
        // The mantissa is split between the integral and fractional part of the number
        fp_digits[0] = mantissa >> (63 - exponent);
    } else if (exponent % 64 == 63) {
        // The mantissa lies within a single u64
        fp_digits[exponent / 64] = mantissa;
        memset(fp_digits, 0, sizeof(u64) * (exponent / 64));
    } else {
        // The mantissa is split between two u64s
        fp_digits[exponent / 64] = mantissa >> (63 - exponent % 64);
        fp_digits[exponent / 64 - 1] = mantissa << (exponent % 64 + 1);
        memset(fp_digits, 0, sizeof(u64) * (exponent / 64 - 1));
    }
    // Convert the integral part to decimal
    // We divide by 10^19 instead of 10 to produce 19 digits at a time.
    dec_digit_groups_num = 0;
    while (fp_digits_num > 0) {
        // Divide the integral part by 10^19 using long division
        u64 remainder = 0;
        for (int i = fp_digits_num - 1; i >= 0; i--) {
            // Set fp_digits[i] = remainder:fp_digits[i] / POW_10_19 and remainder = remainder:fp_digits[i] % POW_10_19 (":" indicates concatenation)
            // This is done in inline assembly since there isn't a good way to divide a 128-bit integer by a 64-bit integer to get a 64-bit result in C.
            asm ("div %[d]"
                : "=a"(fp_digits[i]), "=d"(remainder)
                : [d] "r"(POW_10_19), "d"(remainder), "a"(fp_digits[i])
            );
        }
        // The final remainder is the next digit
        dec_digit_groups[dec_digit_groups_num++] = remainder;
        // If the highest u64 became zero, shrink the bignum's length
        if (fp_digits[fp_digits_num - 1] == 0)
            fp_digits_num -= 1;
    }
    // Print the integral part
    if (dec_digit_groups_num == 0) {
        // If the integral part is zero, print a zero
        printf_char(target, '0');
    } else {
        // Print the first digit group while skipping initial zeroes
        size_t n = dec_digit_groups[dec_digit_groups_num - 1];
        int i = 0;
        for (; n > 0; n /= 10)
            dec_digits[i++] = (n % 10) + '0';
        for (int j = 0; j < i; j++)
            printf_char(target, dec_digits[i - j - 1]);
        // Print the remaining digit groups
        for (int i = dec_digit_groups_num - 2; i >= 0; i--) {
            u64 n = dec_digit_groups[i];
            for (int j = 0; j < 19; j++) {
                dec_digits[j] = (n % 10) + '0';
                n /= 10;
            }
            for (int j = 0; j < 19; j++)
                printf_char(target, dec_digits[18 - j]);
        }
    }
    // Print the decimal point
    printf_char(target, '.');
    // Convert the fractional part to a big-endian big integer
    // This involves shifting the mantissa left by the negated exponent
    fp_digits_num = (- exponent + 62) / 64 + 1;
    if (exponent >= 63) {
        // The mantissa is entirely within the integral part of the number, so the fractional part is zero
        fp_digits_num = 0;
    } else if (exponent >= -1) {
        // The mantissa is split between the integral and fractional part of the number
        fp_digits[0] = mantissa << (exponent + 1);
    } else if (exponent % 64 == -1) {
        // The mantissa lies within a single u64
        fp_digits[(- exponent - 1) / 64] = mantissa;
        memset(fp_digits, 0, sizeof(u64) * ((- exponent - 1) / 64));
    } else {
        // The mantissa is split between two u64s
        fp_digits[(- exponent - 1) / 64] = mantissa >> ((- exponent - 1) % 64);
        fp_digits[(- exponent - 1) / 64 + 1] = mantissa << (64 - (- exponent - 1) % 64);
        memset(fp_digits, 0, sizeof(u64) * ((- exponent - 1) / 64));
    }
    // Multiply the fractional part by 10^19 using long multiplication to get the highest 19 digits
    u64 remainder = 0;
    for (int i = fp_digits_num - 1; i >= 0; i--) {
        // Set remainder:fp_digits[i] = fp_digits[i] * POW_10_19 + remainder (":" indicates concatenation)
        // As with the division, there is no good way to this is C, so inline assembly is used.
        asm ("mul %[m]; add rax, %[r]; adc rdx, 0"
            : "=d"(remainder), "=a"(fp_digits[i])
            : [m] "r"(POW_10_19), [r] "r"(remainder), "a"(fp_digits[i])
        );
    }
    // Convert the remainder to digits and print the first 6
    u64 n = remainder;
    for (int j = 0; j < 19; j++) {
        dec_digits[j] = (n % 10) + '0';
        n /= 10;
    }
    for (int j = 0; j < 6; j++)
        printf_char(target, dec_digits[18 - j]);
}

// Print to a printf target
// This function implements the main logic of all printf() family functions.
static void printf_common(struct printf_target target, const char *restrict fmt, va_list args) {
    size_t i = 0;
    while (1) {
        // Check for end of format string
        if (fmt[i] == '\0')
            return;
        // If the next character is a normal character, print it
        if (fmt[i] != '%') {
            printf_char(target, fmt[i]);
            i++;
            continue;
        }
        i++;
        // Previous character was '%', so the next character is a conversion specifier
        switch (fmt[i++]) {
        case '%':
            printf_char(target, '%');
            break;
        case 'c': {
            int c = va_arg(args, int);
            printf_char(target, (unsigned char)c);
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            printf_string(target, s);
            break;
        }
        case 'd':
        case 'i': {
            int n = va_arg(args, int);
            printf_dec_signed(target, n);
            break;
        }
        case 'o': {
            unsigned int n = va_arg(args, unsigned int);
            printf_oct(target, n);
            break;
        }
        case 'x': {
            unsigned int n = va_arg(args, unsigned int);
            printf_hex(target, n, false);
            break;
        }
        case 'X': {
            unsigned int n = va_arg(args, unsigned int);
            printf_hex(target, n, true);
            break;
        }
        case 'u': {
            unsigned int n = va_arg(args, unsigned int);
            printf_dec(target, n);
            break;
        }
        case 'f': {
            double f = va_arg(args, double);
            printf_float(target, f, false);
            break;
        }
        case 'F': {
            double f = va_arg(args, double);
            printf_float(target, f, true);
            break;
        }
        // Incorrect specifiers have undefined behavior, so we choose to ignore them
        case '\0':
            return;
        default:
            break;
        }
    }
}

int printf(const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);
    return ret;
}

int sprintf(char *restrict buffer, const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsprintf(buffer, format, args);
    va_end(args);
    return ret;
}

int snprintf(char *restrict buffer, size_t size, const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(buffer, size, format, args);
    va_end(args);
    return ret;
}

int vprintf(const char *restrict format, va_list args) {
    size_t offset = 0;
    printf_common((struct printf_target){.to_file = true, .offset = &offset}, format, args);
    return offset;
}

int vsprintf(char *restrict buffer, const char *restrict format, va_list args) {
    // Since no buffer size is provided, we take it to be the largest possible value
    return vsnprintf(buffer, SIZE_MAX, format, args);
}

int vsnprintf(char *restrict buffer, size_t size, const char *restrict format, va_list args) {
    size_t offset = 0;
    // One is subtracted from the size to make room for the null terminator
    printf_common((struct printf_target){.to_file = false, .offset = &offset, .buffer = buffer, .size = size - 1}, format, args);
    // Place the null terminator
    // Offset contains the number of characters printed.
    if (offset < size)
        buffer[offset] = '\0';
    else
        buffer[size - 1] = '\0';
    return offset;
}
