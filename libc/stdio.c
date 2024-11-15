#include <zr/types.h>
#include <stdio.h>

#include <ctype.h>
#include <float.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <zr/syscalls.h>

#include "float_cast.h"
#include "file.h"

static FILE stdout_file = (FILE){.type = FILE_INVALID, .mode = FILE_W};
static FILE stderr_file = (FILE){.type = FILE_INVALID, .mode = FILE_W};
static FILE stdin_file = (FILE){.type = FILE_INVALID, .mode = FILE_R};

FILE *stdout = &stdout_file;
FILE *stderr = &stderr_file;
FILE *stdin = &stdin_file;

// File with an empty buffer
static FILE dummy_file = {
    .type = FILE_BUFFER,
    .mode = FILE_RW,
    .buffer = NULL,
    .buffer_size = 0,
    .buffer_offset = 0,
};

static void create_buffer(FILE *f, BufferMode mode) {
    f->buffer_mode = mode;
    if (mode == _IONBF)
        return;
    f->buffer = malloc(BUFSIZ);
    if (f->buffer == NULL) {
        f->buffer_mode = _IONBF;
        return;
    }
    f->buffer_capacity = BUFSIZ;
    f->buffer_size = 0;
    f->buffer_offset = 0;
}

void _stdio_init(void) {
    err_t err;
    err = resource_get(&resource_name("text/stdout"), RESOURCE_TYPE_CHANNEL_SEND, &stdout->channel);
    if (!err) {
        stdout->type = FILE_CHANNEL;
        create_buffer(stdout, _IOLBF);
    }
    err = resource_get(&resource_name("text/stderr"), RESOURCE_TYPE_CHANNEL_SEND, &stderr->channel);
    if (!err) {
        stderr->type = FILE_CHANNEL;
        create_buffer(stdin, _IONBF);
    }
    err = resource_get(&resource_name("text/stdin"), RESOURCE_TYPE_CHANNEL_SEND, &stdin->channel);
    if (!err) {
        stdin->type = FILE_CHANNEL;
        create_buffer(stdin, _IOLBF);
    }
}

int fputc(int c, FILE *f) {
    err_t err;
    if (f->mode != FILE_W && f->mode != FILE_RW)
        goto fail;
    switch (f->type) {
    case FILE_INVALID:
        goto fail;
    case FILE_BUFFER:
        // Write a byte to the buffer if it's within bounds
        if (f->buffer_offset < f->buffer_size) {
            f->buffer[f->buffer_offset] = (char)c;
            f->buffer_offset++;
        }
        break;
    case FILE_CHANNEL:
        switch (f->buffer_mode) {
        case _IONBF: {
            unsigned char c_ = (unsigned char)c;
            err = channel_call(f->channel, &(SendMessage){1, &(SendMessageData){1, &c_}, 0, NULL}, NULL);
            if (err)
                goto fail;
            break;
        }
        case _IOLBF:
        case _IOFBF:
            f->buffer[f->buffer_offset] = (char)c;
            f->buffer_offset++;
            if (f->buffer_size < f->buffer_offset)
                f->buffer_size = f->buffer_offset;
            if (f->buffer_size >= f->buffer_capacity || (f->buffer_mode == _IOLBF && c == '\n'))
                fflush(f);
            break;
        }
        break;
    }
    return c;
fail:
    f->error = true;
    return EOF;
}

int fgetc(FILE *f) {
    err_t err;
    if (f->mode != FILE_R && f->mode != FILE_RW)
        goto fail;
    if (f->ungetc_buffer_full) {
        f->ungetc_buffer_full = false;
        return f->ungetc_buffer;
    }
    switch (f->type) {
    case FILE_INVALID:
        goto fail;
    case FILE_BUFFER:
        if (f->buffer_offset < f->buffer_size) {
            int c = f->buffer[f->buffer_offset];
            f->buffer_offset++;
            return c;
        } else {
            f->eof = true;
            return EOF;
        }
    case FILE_CHANNEL:
        switch (f->buffer_mode) {
        case _IONBF: {
            size_t requested_size = 1;
            unsigned char c;
            err = channel_call_read(
                f->channel,
                &(SendMessage){1, &(SendMessageData){sizeof(size_t), &requested_size}, 0, NULL},
                &(ReceiveMessage){1, &c, 0, NULL},
                NULL
            );
            if (err)
                goto fail;
            return c;
        }
        case _IOLBF:
        case _IOFBF:
            if (f->buffer_offset >= f->buffer_size) {
                f->buffer_offset = 0;
                ReceiveMessage reply = {f->buffer_capacity, f->buffer, 0, NULL};
                err = channel_call_read(
                    f->channel,
                    &(SendMessage){1, &(SendMessageData){sizeof(size_t), &f->buffer_capacity}, 0, NULL},
                    &reply,
                    &(MessageLength){1, 0}
                );
                if (err) {
                    f->buffer_size = 0;
                    goto fail;
                }
                f->buffer_size = reply.data_length;
            }
            unsigned char c = f->buffer[f->buffer_offset];
            f->buffer_offset++;
            return c;
        }
    }
fail:
    f->error = true;
    return EOF;
}

int ungetc(int c, FILE *f) {
    if (c == EOF || f->ungetc_buffer_full)
        return EOF;
    f->ungetc_buffer = (unsigned char)c;
    f->ungetc_buffer_full = true;
    return c;
}

int putchar(int c) {
    return fputc(c, stdout);
}

int getchar(void) {
    return fgetc(stdin);
}

int fputs(const char *restrict s, FILE *restrict f) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        int ferr = fputc(s[i], f);
        if (ferr == EOF)
            return EOF;
    }
    return 0;
}

int puts(const char *s) {
    int ferr = fputs(s, stdout);
    if (ferr == EOF)
        return EOF;
    return fputc('\n', stdout);
}

char *fgets(char *restrict s, int n, FILE *restrict f) {
    int i = 0;
    for (; i < n - 1; i++) {
        int c = fgetc(f);
        if (c == EOF) {
            if (f->error || i == 0)
                return NULL;
            else
                break;
        }
        s[i] = (char)c;
        if (c == '\n') {
            i++;
            break;
        }
    }
    s[i] = '\0';
    return s;
}

static void printf_char(FILE *file, size_t *offset, char c) {
    (*offset)++;
    fputc(c, file);
}

// Print enough padding to pad field of length `length` to fill `field_width` characters
static void printf_padding(FILE *file, size_t *offset, size_t field_width, size_t length, size_t *padding_zeroes) {
    if (length < field_width) {
        if (padding_zeroes) {
            *padding_zeroes = field_width - length;
        } else {
            for (size_t i = 0; i < field_width - length; i++)
                printf_char(file, offset, ' ');
        }
    }
}

// Print a null-terminated string
static void printf_string(FILE *file, size_t *offset, const char *s, int precision) {
    for (size_t i = 0; i < (size_t)precision && s[i] != '\0'; i++)
        printf_char(file, offset, s[i]);
}

typedef enum PrintfFlags {
    PRINTF_SIGN_ALWAYS = 1,
    PRINTF_SIGN_SPACE = 2,
    PRINTF_ALTERNATIVE_FORM = 4,
} PrintfFlags;

// Print an unsinged decimal number
static void printf_dec(FILE *file, size_t *offset, uintmax_t n, int precision, size_t padding_zeroes) {
    // Print padding zeroes
    for (size_t i = 0; i < padding_zeroes; i++)
        printf_char(file, offset, '0');
    // Since the digits will be generated in the reverse order from the one we need, we put them in a buffer before printing them.
    // 2.41 is a little above the decimal logarithm of 256 (the number of decimal digits a byte contains).
    char digits[(size_t)(sizeof(uintmax_t) * 2.41 + 1)];
    int i = 0;
    // Generate the digits
    for (; n > 0; n /= 10)
        digits[i++] = (n % 10) + '0';
    // Print leading zeroes
    if (i < precision)
        for (int j = i; j < precision; j++)
            printf_char(file, offset, '0');
    // Print the digits in reverse order
    for (int j = 0; j < i; j++)
        printf_char(file, offset, digits[i - j - 1]);
}

// Print a singed decimal number
static void printf_dec_signed(FILE *file, size_t *offset, intmax_t n, int precision, PrintfFlags flags, size_t padding_zeroes) {
    uintmax_t nu; // Absolute value of n
    // If the number is negative, print a minus sign and invert it
    if (n < 0) {
        printf_char(file, offset, '-');
        nu = -n;
    } else {
        if (flags & PRINTF_SIGN_ALWAYS)
            printf_char(file, offset, '+');
        else if (flags & PRINTF_SIGN_SPACE)
            printf_char(file, offset, ' ');
        nu = n;
    }
    // Print the number without the sign
    printf_dec(file, offset, nu, precision, padding_zeroes);
}

// Print an unsinged octal number
static void printf_oct(FILE *file, size_t *offset, uintmax_t n, int precision, PrintfFlags flags, size_t padding_zeroes) {
    // Print padding zeroes
    for (size_t i = 0; i < padding_zeroes; i++)
        printf_char(file, offset, '0');
    // Index of octal digit
    int i = (8 * sizeof(uintmax_t) - 1) / 3;
    // Skip initial zeroes
    for (; i >= 0 && ((n >> (3 * i)) & 0x7) == 0; i--)
        ;
    // Print leading zeroes
    if (i + 1 < precision)
        for (int j = i + 1; j < precision; j++)
            printf_char(file, offset, '0');
    else if (flags & PRINTF_ALTERNATIVE_FORM)
        printf_char(file, offset, '0');
    // Print digits
    for (; i >= 0; i--) {
        int digit = (n >> (3 * i)) & 0x7;
        printf_char(file, offset, digit + '0');
    }
}

// Print an unsinged hexadecimal number
// `uppercase` determines whether digits above 9 are printed as "ABCDEF" or "abcdef".
static void printf_hex(FILE *file, size_t *offset, uintmax_t n, bool uppercase, int precision, PrintfFlags flags, size_t padding_zeroes) {
    // Print prefix for alternative form
    if (flags & PRINTF_ALTERNATIVE_FORM)
        printf_string(file, offset, uppercase ? "0X" : "0x", -1);
    // Print padding zeroes
    for (size_t i = 0; i < padding_zeroes; i++)
        printf_char(file, offset, '0');
    // Index of hexadecimal digit
    int i = 2 * sizeof(uintmax_t) - 1;
    // Skip initial zeroes
    for (; i >= 0 && ((n >> (4 * i)) & 0xF) == 0; i--)
        ;
    // Print leading zeroes
    if (i + 1 < precision)
        for (int j = i + 1; j < precision; j++)
            printf_char(file, offset, '0');
    // Print digits
    for (; i >= 0; i--) {
        int digit = (n >> (4 * i)) & 0xF;
        printf_char(file, offset, digit < 10 ? digit + '0' : digit - 10 + (uppercase ? 'A' : 'a'));
    }
}

// Verify that long double is 80-bit extended precision format
// The code for printing floating point numbers relies on this assumption.
#if !(FLT_RADIX == 2 && LDBL_MANT_DIG == 64 && LDBL_MIN_EXP == -16381 && LDBL_MAX_EXP == 16384)
#error "long double is not 80-bit extended precision format"
#endif

// 10^19 - the largest power of 10 that can fit in a u64
#define POW_10_19 UINT64_C(10000000000000000000)

typedef enum FloatRepr {
    FLOAT_REPR_F,
    FLOAT_REPR_E,
    FLOAT_REPR_G,
    FLOAT_REPR_A,
} FloatRepr;

// Print a floating-point number
// `uppercase` determines whether nan and inf are printed in lowercase or uppercase.
// The conversion algorithm used is very basic and does not properly round the result, but only truncates it.
static void printf_float(FILE *file, size_t *offset, long double f, bool uppercase, FloatRepr repr, int precision, PrintfFlags flags, size_t padding_zeroes) {
    // Extract mantissa and exponent fields
    union long_double_cast f_cast;
    f_cast.f = f;
    u64 mantissa = f_cast.mantissa;
    u16 exponent_field = f_cast.sign_exponent & 0x7FFF;
    // If the sign bit is set, print a minus sign
    if (f_cast.sign_exponent & 0x8000)
        printf_char(file, offset, '-');
    else if (flags & PRINTF_SIGN_ALWAYS)
        printf_char(file, offset, '+');
    else if (flags & PRINTF_SIGN_SPACE)
        printf_char(file, offset, ' ');
    // Handle NaN and infinity
    if (exponent_field == 0x7FFF) {
        // Print padding zeroes as spaces
        for (size_t i = 0; i < padding_zeroes; i++)
            printf_char(file, offset, ' ');
        // When checking the mantissa, ignore the highest bit, since it should be 1
        if ((mantissa & (UINT64_C(-1) >> 1)) == 0)
            printf_string(file, offset, uppercase ? "INF" : "inf", -1);
        else
            printf_string(file, offset, uppercase ? "NAN" : "nan", -1);
        return;
    }
    // Print hexadecimal prefix
    if (repr == FLOAT_REPR_A)
        printf_string(file, offset, uppercase ? "0X" : "0x", -1);
    // Print padding zeroes
    for (size_t i = 0; i < padding_zeroes; i++)
        printf_char(file, offset, '0');
    // In order to avoid going into an infinite loop when printing the fractional part, handle zeroes in exponential representations separately
    if (exponent_field == 0 && (mantissa & (UINT64_C(-1) >> 1)) == 0) {
        if (repr == FLOAT_REPR_E) {
            printf_char(file, offset, '0');
            if (precision != 0 || (flags & PRINTF_ALTERNATIVE_FORM)) {
                printf_char(file, offset, '.');
                for (int i = 0; i < precision; i++)
                    printf_char(file, offset, '0');
            }
            printf_char(file, offset, uppercase ? 'E' : 'e');
            printf_string(file, offset, "+00", -1);
            return;
        } else if (repr == FLOAT_REPR_G) {
            printf_char(file, offset, '0');
            if (flags & PRINTF_ALTERNATIVE_FORM) {
                printf_char(file, offset, '.');
                for (int i = 0; i < precision - 1; i++)
                    printf_char(file, offset, '0');
            }
            return;
        }
    }
    // Subtract the bias from the exponent
    i32 exponent = exponent_field - 16383;
    // If the number is denormal, adjust the exponent to account for it
    // There is no need to change anything else, since the integral part of the mantissa is already explicitly stored in this format.
    if (exponent_field == 0)
        exponent += 1;
    // Handle hexadecimal representation separately
    if (repr == FLOAT_REPR_A) {
        // Set exponent for 0 to be 0
        if (exponent_field == 0 && (mantissa & (UINT64_C(-1) >> 1)) == 0)
            exponent = 0;
        // Print first digit
        printf_char(file, offset, '0' + (mantissa >> 63));
        mantissa <<= 1;
        // Print decimal point
        if (precision != 0 || (flags & PRINTF_ALTERNATIVE_FORM))
            printf_char(file, offset, '.');
        // Print remaining digits
        for (int i = 0; i < precision; i++) {
            int digit = mantissa >> 60;
            printf_char(file, offset, digit < 10 ? digit + '0' : digit - 10 + (uppercase ? 'A' : 'a'));
            mantissa <<= 4;
        }
        // Print exponent
        printf_char(file, offset, uppercase ? 'P' : 'p');
        printf_char(file, offset, exponent >= 0 ? '+' : '-');
        printf_dec(file, offset, exponent >= 0 ? exponent : -exponent, 1, 0);
        return;
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
        // Divide the integral part by 10^19
        u64 remainder = 0;
        for (int i = fp_digits_num - 1; i >= 0; i--) {
            // Set fp_digits[i] = remainder:fp_digits[i] / POW_10_19 and remainder = remainder:fp_digits[i] % POW_10_19 (":" indicates concatenation)
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
    bool exponential = repr == FLOAT_REPR_E;
    bool got_decimal_point = false;
    int dec_exponent = 0;
    int digits_printed = 0;
    bool decimal_point_skipped = false;
    int zeroes_skipped = 0;
    if (dec_digit_groups_num == 0) {
        // If the integral part is zero, print a zero
        if (repr == FLOAT_REPR_F)
            printf_char(file, offset, '0');
        else
            dec_exponent = -1;
    } else {
        // Print the first digit group while skipping initial zeroes
        int initial_limit = 0;
        for (u64 n = dec_digit_groups[dec_digit_groups_num - 1]; n > 0; n /= 10)
            dec_digits[initial_limit++] = (n % 10) + '0';
        dec_exponent = 19 * (dec_digit_groups_num - 1) + initial_limit - 1;
        if (repr == FLOAT_REPR_G) {
            if (dec_exponent >= precision) {
                exponential = true;
                precision--;
            } else {
                precision -= 1 + dec_exponent;
            }
        }
        if (exponential) {
            // First digit in exponential form has decimal point after it
            printf_char(file, offset, dec_digits[initial_limit - 1]);
            initial_limit--;
            decimal_point_skipped = true;
            got_decimal_point = true;
        }
        // Print the digit groups
        for (int i = dec_digit_groups_num - 1; i >= 0; i--) {
            u64 n = dec_digit_groups[i];
            for (int j = 0; j < 19; j++) {
                dec_digits[j] = (n % 10) + '0';
                n /= 10;
            }
            int limit = i == dec_digit_groups_num - 1 ? initial_limit : 19;
            for (int j = 0; j < limit; j++) {
                if (repr == FLOAT_REPR_G && exponential && dec_digits[limit - 1 - j] == '0') {
                    if (digits_printed >= precision)
                        goto end_digits;
                    zeroes_skipped++;
                    digits_printed++;
                } else {
                    if (exponential && digits_printed >= precision)
                        goto end_digits;
                    if (decimal_point_skipped) {
                        printf_char(file, offset, '.');
                        decimal_point_skipped = false;
                    }
                    for (; zeroes_skipped > 0; zeroes_skipped--)
                        printf_char(file, offset, '0');
                    printf_char(file, offset, dec_digits[limit - 1 - j]);
                    if (exponential)
                        digits_printed++;
                }
            }
        }
    }
    if (repr == FLOAT_REPR_F || (repr == FLOAT_REPR_G && dec_digit_groups_num != 0 && !exponential)) {
        // Print the decimal point
        decimal_point_skipped = true;
        got_decimal_point = true;
    }
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
    // Print the fractional part
    while (1) {
        // Multiply the fractional part by 10^19 to get the highest 19 digits
        u64 remainder = 0;
        for (int i = fp_digits_num - 1; i >= 0; i--) {
            // Set remainder:fp_digits[i] = fp_digits[i] * POW_10_19 + remainder (":" indicates concatenation)
            asm ("mul %[m]; add rax, %[r]; adc rdx, 0"
                : "=&d"(remainder), "=&a"(fp_digits[i])
                : [m] "r"(POW_10_19), [r] "r"(remainder), "a"(fp_digits[i])
            );
        }
        // Convert the remainder to digits and print the digits
        u64 n = remainder;
        for (int j = 0; j < 19; j++) {
            dec_digits[j] = (n % 10) + '0';
            n /= 10;
        }
        for (int j = 0; j < 19; j++) {
            if (repr == FLOAT_REPR_G && !got_decimal_point && !exponential) {
                if (dec_digits[18 - j] != '0') {
                    printf_char(file, offset, '0');
                    printf_char(file, offset, '.');
                    for (int i = -1; i > dec_exponent; i--)
                        printf_char(file, offset, '0');
                    printf_char(file, offset, dec_digits[18 - j]);
                    got_decimal_point = true;
                    precision -= 1 + dec_exponent;
                } else {
                    dec_exponent--;
                    if (dec_exponent <= -4) {
                        exponential = true;
                        precision--;
                    }
                }
            } else if (exponential) {
                if (repr == FLOAT_REPR_G && got_decimal_point && dec_digits[18 - j] == '0') {
                    if (digits_printed >= precision)
                        goto end_digits;
                    zeroes_skipped++;
                    digits_printed++;
                } else if (got_decimal_point) {
                    if (digits_printed >= precision)
                        goto end_digits;
                    if (decimal_point_skipped) {
                        printf_char(file, offset, '.');
                        decimal_point_skipped = false;
                    }
                    for (; zeroes_skipped > 0; zeroes_skipped--)
                        printf_char(file, offset, '0');
                    printf_char(file, offset, dec_digits[18 - j]);
                    digits_printed++;
                } else if (dec_digits[18 - j] != '0') {
                    printf_char(file, offset, dec_digits[18 - j]);
                    decimal_point_skipped = true;
                    got_decimal_point = true;
                } else {
                    dec_exponent--;
                }
            } else {
                if (repr == FLOAT_REPR_G && dec_digits[18 - j] == '0') {
                    if (digits_printed >= precision)
                        goto end_digits;
                    zeroes_skipped++;
                    digits_printed++;
                } else {
                    if (digits_printed >= precision)
                        goto end_digits;
                    if (decimal_point_skipped) {
                        printf_char(file, offset, '.');
                        decimal_point_skipped = false;
                    }
                    for (; zeroes_skipped > 0; zeroes_skipped--)
                        printf_char(file, offset, '0');
                    printf_char(file, offset, dec_digits[18 - j]);
                    digits_printed++;
                }
            }
        }
    }
end_digits:
    if (flags & PRINTF_ALTERNATIVE_FORM) {
        if (decimal_point_skipped) {
            printf_char(file, offset, '.');
            decimal_point_skipped = false;
        }
        for (; zeroes_skipped > 0; zeroes_skipped--)
            printf_char(file, offset, '0');
    }
    if (exponential) {
        printf_char(file, offset, uppercase ? 'E' : 'e');
        printf_char(file, offset, dec_exponent >= 0 ? '+' : '-');
        printf_dec(file, offset, dec_exponent >= 0 ? dec_exponent : -dec_exponent, 2, 0);
    }
}

typedef enum LengthModifier {
    LENGTH_MOD_NONE,
    LENGTH_MOD_h,
    LENGTH_MOD_hh,
    LENGTH_MOD_l,
    LENGTH_MOD_ll,
    LENGTH_MOD_j,
    LENGTH_MOD_z,
    LENGTH_MOD_t,
    LENGTH_MOD_L,
} LengthModifier;

static LengthModifier read_length_modifier(const char *fmt, size_t *i) {
    switch (fmt[*i]) {
    case 'h':
        (*i)++;
        if (fmt[*i] == 'h') {
            (*i)++;
            return LENGTH_MOD_hh;
        } else {
            return LENGTH_MOD_h;
        }
    case 'l':
        (*i)++;
        if (fmt[*i] == 'l') {
            (*i)++;
            return LENGTH_MOD_ll;
        } else {
            return LENGTH_MOD_l;
        }
    case 'j':
        (*i)++;
        return LENGTH_MOD_j;
    case 'z':
        (*i)++;
        return LENGTH_MOD_z;
    case 't':
        (*i)++;
        return LENGTH_MOD_t;
    case 'L':
        (*i)++;
        return LENGTH_MOD_L;
    default:
        return LENGTH_MOD_NONE;
    }
}

// Number of hex digits necessary to represent a pointer value
#define PTR_HEX_DIGITS (2 * sizeof(void *))

// Print to a printf target
// This function implements the main logic of all printf() family functions.
static void printf_common(FILE *file, size_t *offset, const char *fmt, va_list args) {
    size_t i = 0;
    while (1) {
        // Check for end of format string
        if (fmt[i] == '\0')
            return;
        // If the next character is a normal character, print it
        if (fmt[i] != '%') {
            printf_char(file, offset, fmt[i]);
            i++;
            continue;
        }
        i++;
        // Read flags
        bool left_justify = false;
        bool zero_pad = false;
        PrintfFlags flags = 0;
        while (1) {
            switch (fmt[i]) {
            case '-':
                left_justify = true;
                i++;
                break;
            case '+':
                flags |= PRINTF_SIGN_ALWAYS;
                i++;
                break;
            case ' ':
                flags |= PRINTF_SIGN_SPACE;
                i++;
                break;
            case '#':
                flags |= PRINTF_ALTERNATIVE_FORM;
                i++;
                break;
            case '0':
                zero_pad = true;
                i++;
                break;
            default:
                goto flags_end;
            }
        }
flags_end:
        if (flags & PRINTF_SIGN_ALWAYS)
            flags &= ~PRINTF_SIGN_SPACE;
        // Read field width
        bool got_field_width = false;
        size_t field_width = 0;
        if (fmt[i] == '*') {
            got_field_width = true;
            int n = va_arg(args, int);
            if (n >= 0) {
                field_width = n;
            } else {
                left_justify = true;
                field_width = -n;
            }
            i++;
        } else {
            while ('0' <= fmt[i] && fmt[i] <= '9') {
                got_field_width = true;
                field_width = 10 * field_width + (fmt[i] - '0');
                i++;
            }
        }
        if (left_justify)
            zero_pad = false;
        // Read precision
        bool got_precision = false;
        int precision = 0;
        if (fmt[i] == '.') {
            got_precision = true;
            i++;
            if (fmt[i] == '*') {
                precision = va_arg(args, int);
                i++;
            } else {
                while ('0' <= fmt[i] && fmt[i] <= '9') {
                    precision = 10 * precision + (fmt[i] - '0');
                    i++;
                }
            }
            zero_pad = false;
        }
        if (precision < 0)
            precision = 0;
        // Read the length modifier
        LengthModifier length_modifier = read_length_modifier(fmt, &i);
        // Read the conversion specifier
        char specifier = fmt[i];
        i++;
        size_t offset_before = *offset;
        switch (specifier) {
        case '%':
            if (length_modifier != LENGTH_MOD_NONE)
                return;
            if (got_field_width && !left_justify)
                printf_padding(file, offset, field_width, 1, NULL);
            printf_char(file, offset, '%');
            break;
        case 'c': {
            if (length_modifier != LENGTH_MOD_NONE)
                return;
            int c = va_arg(args, int);
            if (got_field_width && !left_justify)
                printf_padding(file, offset, field_width, 1, NULL);
            printf_char(file, offset, (unsigned char)c);
            break;
        }
        case 's': {
            if (length_modifier != LENGTH_MOD_NONE)
                return;
            const char *s = va_arg(args, const char *);
            if (got_field_width && !left_justify)
                printf_padding(file, offset, field_width, strlen(s), NULL);
            printf_string(file, offset, s, got_precision ? precision : -1);
            break;
        }
        case 'd':
        case 'i': {
            // Read argument
            intmax_t n;
            switch (length_modifier) {
            case LENGTH_MOD_hh:
            case LENGTH_MOD_h:
            case LENGTH_MOD_NONE:
                n = (intmax_t)va_arg(args, int);
                break;
            case LENGTH_MOD_l:
                n = (intmax_t)va_arg(args, long);
                break;
            case LENGTH_MOD_ll:
                n = (intmax_t)va_arg(args, long long);
                break;
            case LENGTH_MOD_j:
                n = va_arg(args, intmax_t);
                break;
            case LENGTH_MOD_t:
                n = (intmax_t)va_arg(args, ptrdiff_t);
                break;
            case LENGTH_MOD_z: {
                uintmax_t un = (uintmax_t)va_arg(args, size_t);
                // Sign-extend if necessary
                if (sizeof(size_t) < sizeof(uintmax_t))
                    n = (intmax_t)(un | ((un >> (8 * sizeof(size_t) - 1)) * (UINTMAX_C(-1) << (8 * sizeof(size_t)))));
                else
                    n = (intmax_t)un;
                break;
            case LENGTH_MOD_L:
                return;
            }
            }
            // Print number
            if (!got_precision)
                precision = 1;
            size_t padding_zeroes = 0;
            if (got_field_width && !left_justify) {
                size_t length = 0;
                printf_dec_signed(&dummy_file, &length, n, precision, flags, 0);
                printf_padding(file, offset, field_width, length, zero_pad ? &padding_zeroes : NULL);
            }
            printf_dec_signed(file, offset, n, precision, flags, padding_zeroes);
            break;
        }
        case 'o':
        case 'x':
        case 'X':
        case 'u': {
            // Read argument
            uintmax_t n;
            switch (length_modifier) {
            case LENGTH_MOD_hh:
            case LENGTH_MOD_h:
            case LENGTH_MOD_NONE:
                n = (uintmax_t)va_arg(args, unsigned int);
                break;
            case LENGTH_MOD_l:
                n = (uintmax_t)va_arg(args, unsigned long);
                break;
            case LENGTH_MOD_ll:
                n = (uintmax_t)va_arg(args, unsigned long long);
                break;
            case LENGTH_MOD_j:
                n = va_arg(args, uintmax_t);
                break;
            case LENGTH_MOD_z: {
                n = (uintmax_t)va_arg(args, size_t);
                break;
            }
            case LENGTH_MOD_t:
                n = (uintmax_t)va_arg(args, ptrdiff_t);
                // Remove sign extension if necessary
                if (sizeof(ptrdiff_t) < sizeof(uintmax_t))
                    n &= (UINTMAX_C(-1) >> (8 * (sizeof(uintmax_t) - sizeof(ptrdiff_t))));
                break;
            case LENGTH_MOD_L:
                return;
            }
            // Print number
            if (!got_precision)
                precision = 1;
            size_t padding_zeroes = 0;
            if (got_field_width && !left_justify) {
                size_t length = 0;
                switch (specifier) {
                case 'o':
                    printf_oct(&dummy_file, &length, n, precision, flags, 0);
                    break;
                case 'x':
                    printf_hex(&dummy_file, &length, n, false, precision, flags, 0);
                    break;
                case 'X':
                    printf_hex(&dummy_file, &length, n, true, precision, flags, 0);
                    break;
                case 'u':
                    printf_dec(&dummy_file, &length, n, precision, 0);
                    break;
                }
                printf_padding(file, offset, field_width, length, zero_pad ? &padding_zeroes : NULL);
            }
            switch (specifier) {
            case 'o':
                printf_oct(file, offset, n, precision, flags, padding_zeroes);
                break;
            case 'x':
                printf_hex(file, offset, n, false, precision, flags, padding_zeroes);
                break;
            case 'X':
                printf_hex(file, offset, n, true, precision, flags, padding_zeroes);
                break;
            case 'u':
                printf_dec(file, offset, n, precision, padding_zeroes);
                break;
            }
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            // Read argument
            long double f;
            switch (length_modifier) {
            case LENGTH_MOD_NONE:
            case LENGTH_MOD_l:
                f = (long double)va_arg(args, double);
                break;
            case LENGTH_MOD_L:
                f = va_arg(args, long double);
                break;
            default:
                return;
            }
            // Get properties
            bool uppercase;
            FloatRepr repr;
            switch (specifier) {
            case 'f':
                uppercase = false;
                repr = FLOAT_REPR_F;
                break;
            case 'F':
                uppercase = true;
                repr = FLOAT_REPR_F;
                break;
            case 'e':
                uppercase = false;
                repr = FLOAT_REPR_E;
                break;
            case 'E':
                uppercase = true;
                repr = FLOAT_REPR_E;
                break;
            case 'g':
                uppercase = false;
                repr = FLOAT_REPR_G;
                break;
            case 'G':
                uppercase = true;
                repr = FLOAT_REPR_G;
                break;
            case 'a':
                uppercase = false;
                repr = FLOAT_REPR_A;
                break;
            case 'A':
                uppercase = true;
                repr = FLOAT_REPR_A;
                break;
            }
            if (!got_precision) {
                if (specifier == 'a' || specifier == 'A') {
                    if (length_modifier == LENGTH_MOD_L)
                        precision = 16;
                    else
                        precision = 13;
                } else {
                    precision = 6;
                }
            }
            // Print number
            size_t padding_zeroes = 0;
            if (got_field_width && !left_justify) {
                size_t length = 0;
                printf_float(&dummy_file, &length, f, uppercase, repr, precision, flags, 0);
                printf_padding(file, offset, field_width, length, zero_pad ? &padding_zeroes : NULL);
            }
            printf_float(file, offset, f, uppercase, repr, precision, flags, padding_zeroes);
            break;
        }
        case 'p': {
            if (length_modifier != LENGTH_MOD_NONE)
                return;
            void *p = va_arg(args, void *);
            if (got_field_width && !left_justify)
                printf_padding(file, offset, field_width, 2 + PTR_HEX_DIGITS, NULL);
            printf_string(file, offset, "0x", -1);
            for (int i = PTR_HEX_DIGITS - 1; i >= 0; i--) {
                int digit = ((uintptr_t)p >> (4 * i)) & 0xF;
                printf_char(file, offset, digit < 10 ? digit + '0' : digit - 10 + 'a');
            }
            break;
        }
        case 'n':
            switch (length_modifier) {
            case LENGTH_MOD_hh:
                *va_arg(args, signed char *) = (signed char)*offset;
                break;
            case LENGTH_MOD_h:
                *va_arg(args, short *) = (short)*offset;
                break;
            case LENGTH_MOD_NONE:
                *va_arg(args, int *) = (int)*offset;
                break;
            case LENGTH_MOD_l:
                *va_arg(args, long *) = (long)*offset;
                break;
            case LENGTH_MOD_ll:
                *va_arg(args, long long *) = (long long)*offset;
                break;
            case LENGTH_MOD_j:
                *va_arg(args, intmax_t *) = (intmax_t)*offset;
                break;
            case LENGTH_MOD_z:
                *va_arg(args, size_t *) = *offset;
                break;
            case LENGTH_MOD_t:
                *va_arg(args, ptrdiff_t *) = (ptrdiff_t)*offset;
                break;
            case LENGTH_MOD_L:
                return;
            }
            break;
        // Incorrect specifiers have undefined behavior, so we choose to fail if we encounter one
        default:
            return;
        }
        if (got_field_width && left_justify)
            printf_padding(file, offset, field_width, *offset - offset_before, NULL);
    }
}

int printf(const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vprintf(format, args);
    va_end(args);
    return ret;
}

int fprintf(FILE *restrict f, const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfprintf(f, format, args);
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
    return vfprintf(stdout, format, args);
}

int vfprintf(FILE *restrict f, const char *restrict format, va_list args) {
    size_t offset = 0;
    printf_common(f, &offset, format, args);
    if (f->error)
        return -1;
    return offset;
}

int vsprintf(char *restrict buffer, const char *restrict format, va_list args) {
    // Since no buffer size is provided, we take it to be the largest possible value
    return vsnprintf(buffer, SIZE_MAX, format, args);
}

int vsnprintf(char *restrict buffer, size_t size, const char *restrict format, va_list args) {
    size_t offset = 0;
    // Create file struct for writing to the buffer
    // One is subtracted from the size to make room for the null terminator.
    FILE file = {
        .type = FILE_BUFFER,
        .mode = FILE_W,
        .buffer = buffer,
        .buffer_size = size > 0 ? size - 1 : 0,
        .buffer_offset = 0,
    };
    printf_common(&file, &offset, format, args);
    // Place the null terminator
    // Offset contains the number of characters printed.
    if (size > 0) {
        if (offset < size)
            buffer[offset] = '\0';
        else
            buffer[size - 1] = '\0';
    }
    return offset;
}

static int scanf_char(FILE *file, size_t *offset, size_t *field_width) {
    if (field_width != NULL) {
        if (*field_width == 0)
            return EOF;
        (*field_width)--;
    }
    (*offset)++;
    return fgetc(file);
}

static void scanf_ungetc(FILE *file, size_t *offset, size_t *field_width, int c) {
    if (field_width != NULL)
        (*field_width)++;
    (*offset)--;
    ungetc(c, file);
}

static void scanf_whitespace(FILE *file, size_t *offset) {
    while (1) {
        int c = scanf_char(file, offset, NULL);
        if (!isspace(c)) {
            scanf_ungetc(file, offset, NULL, c);
            break;
        }
    }
}

static bool scanf_int(FILE *file, size_t *offset, size_t *field_width, uintmax_t *n_ptr, int base) {
    int c;
    // Read sign
    bool negate;
    c = scanf_char(file, offset, field_width);
    switch (c) {
    case '+':
        negate = false;
        break;
    case '-':
        negate = true;
        break;
    default:
        negate = false;
        scanf_ungetc(file, offset, field_width, c);
        break;
    }
    // Read base prefix
    bool has_digits = false;
    if (base == 0 || base == 8 || base == 16) {
        c = scanf_char(file, offset, field_width);
        if (c == '0') {
            if (base == 0 || base == 16) {
                c = scanf_char(file, offset, field_width);
                if (c == 'x' || c == 'X') {
                    if (base == 0)
                        base = 16;
                } else {
                    scanf_ungetc(file, offset, field_width, c);
                    if (base == 0)
                        base = 8;
                    has_digits = true;
                }
            } else {
                has_digits = true;
            }
        } else {
            scanf_ungetc(file, offset, field_width, c);
            if (base == 0)
                base = 10;
        }
    }
    // Read digits
    uintmax_t number = 0;
    while (1) {
        c = scanf_char(file, offset, field_width);
        int digit;
        if ('0' <= c && c <= '9')
            digit = c - '0';
        else if ('a' <= c && c <= 'z')
            digit = c - 'a' + 10;
        else if ('A' <= c && c <= 'Z')
            digit = c - 'A' + 10;
        else
            digit = -1;
        if (digit < 0 || digit >= base) {
            scanf_ungetc(file, offset, field_width, c);
            if (has_digits)
                *n_ptr = negate ? -number : number;
            return has_digits;
        }
        number = base * number + digit;
        has_digits = true;
    }
}

static ptrdiff_t scanf_exponent(FILE *file, size_t *offset, size_t *field_width, size_t *extra_chars) {
    int c;
    // Read sign
    bool got_sign = false;
    bool exponent_sign;
    c = scanf_char(file, offset, field_width);
    switch (c) {
    case '+':
        got_sign = true;
        exponent_sign = false;
        break;
    case '-':
        got_sign = true;
        exponent_sign = true;
        break;
    default:
        exponent_sign = false;
        scanf_ungetc(file, offset, field_width, c);
        break;
    }
    // Read magnitude
    ptrdiff_t exponent = 0;
    size_t exponent_digits_read = 0;
    while (1) {
        c = scanf_char(file, offset, field_width);
        if ('0' <= c && c <= '9') {
            exponent = 10 * exponent + (c - '0');
            exponent_digits_read++;
        } else {
            scanf_ungetc(file, offset, field_width, c);
            break;
        }
    }
    if (exponent_digits_read == 0)
        *extra_chars += 1 + got_sign;
    // if we read more than 18 digits, consider the exponent as having overflow and clamp it to 10^18
    if (exponent_digits_read > 18)
        exponent = 1000000000000000000;
    if (exponent_sign)
        exponent *= -1;
    return exponent;
}

// Returns the number of characters scanned past the end of the valid representation, or SIZE_MAX on failure.
size_t __scanf_float(FILE *file, size_t *offset, size_t *field_width, long double *f_ptr) {
    int c;
    size_t extra_chars = 0;
    union long_double_cast f_cast;
    // Read sign
    bool sign;
    c = scanf_char(file, offset, field_width);
    switch (c) {
    case '+':
        sign = false;
        break;
    case '-':
        sign = true;
        break;
    default:
        sign = false;
        scanf_ungetc(file, offset, field_width, c);
        break;
    }
    // Check first character after sign
    bool got_digit = false;
    c = scanf_char(file, offset, field_width);
    if (tolower(c) == 'i') {
        // Parse infinity
        for (const char *s = "nf"; *s != '\0'; s++) {
            c = scanf_char(file, offset, field_width);
            if (tolower(c) != *s) {
                scanf_ungetc(file, offset, field_width, c);
                return SIZE_MAX;
            }
        }
        extra_chars = 0;
        for (const char *s = "inity"; *s != '\0'; s++) {
            c = scanf_char(file, offset, field_width);
            if (tolower(c) != *s) {
                scanf_ungetc(file, offset, field_width, c);
                goto return_inf;
            }
            extra_chars++;
        }
        extra_chars = 0;
        goto return_inf;
    } else if (tolower(c) == 'n') {
        // Parse NaN
        for (const char *s = "an"; *s != '\0'; s++) {
            c = scanf_char(file, offset, field_width);
            if (tolower(c) != *s) {
                scanf_ungetc(file, offset, field_width, c);
                return SIZE_MAX;
            }
        }
        c = scanf_char(file, offset, field_width);
        if (c == '(') {
            extra_chars = 1;
            while (1) {
                c = scanf_char(file, offset, field_width);
                if (c == ')') {
                    extra_chars = 0;
                    break;
                } else if (!(isalnum(c) || c == '_')) {
                    scanf_ungetc(file, offset, field_width, c);
                    break;
                }
                extra_chars++;
            }
        } else {
            scanf_ungetc(file, offset, field_width, c);
        }
        f_cast.mantissa = UINT64_C(-1);
        f_cast.sign_exponent = sign ? 0xFFFF : 0x7FFF;
        *f_ptr = f_cast.f;
        return extra_chars;
    } else if (c == '0') {
        c = scanf_char(file, offset, field_width);
        if (c == 'x' || c == 'X') {
            // Read hexadecimal floating-point number
            // Skip initial zeroes
            bool got_decimal_point = false;
            ptrdiff_t exponent = -1;
            while (1) {
                c = scanf_char(file, offset, field_width);
                if (c == '0') {
                    got_digit = true;
                    if (got_decimal_point)
                        exponent -= 4;
                } else if (c == '.' && !got_decimal_point) {
                    got_decimal_point = true;
                } else {
                    scanf_ungetc(file, offset, field_width, c);
                    break;
                }
            }
            // Read digits
            u64 mantissa = 0;
            int digit_shift = 60;
            while (1) {
                c = scanf_char(file, offset, field_width);
                int digit;
                if ('0' <= c && c <= '9') {
                    digit = c - '0';
                } else if ('a' <= c && c <= 'f') {
                    digit = c - 'a' + 10;
                } else if ('A' <= c && c <= 'F') {
                    digit = c - 'A' + 10;
                } else if (c == '.' && !got_decimal_point) {
                    // Decimal point
                    got_decimal_point = true;
                    continue;
                } else {
                    // Unexpected character
                    scanf_ungetc(file, offset, field_width, c);
                    break;
                }
                // Add digit to mantissa
                got_digit = true;
                if (!got_decimal_point)
                    exponent += 4;
                if (digit_shift >= 0)
                    mantissa |= (u64)digit << digit_shift;
                else if (digit_shift > -4)
                    mantissa |= (u64)digit >> -digit_shift;
                digit_shift -= 4;
                // Normalize after reading first digit
                if (digit_shift == 56) {
                    while ((mantissa & (UINT64_C(1) << 63)) == 0) {
                        mantissa <<= 1;
                        digit_shift++;
                        exponent--;
                    }
                }
            }
            // Fail if we didn't find any digits
            if (!got_digit)
                return SIZE_MAX;
            // Return zero if there are no non-zero digits
            if ((mantissa & (UINT64_C(1) << 63)) == 0)
                goto return_zero;
            // Read exponent
            c = scanf_char(file, offset, field_width);
            if (c == 'p' || c == 'P')
                exponent += scanf_exponent(file, offset, field_width, &extra_chars);
            else
                scanf_ungetc(file, offset, field_width, c);
            // Assemble value
            if (exponent > 16383) {
                // Exponent is too large to fit, so we return infinity
                goto return_inf;
            } else if (exponent >= -16382) {
                // Number is normal
                f_cast.mantissa = mantissa;
                f_cast.sign_exponent = exponent + 16383;
            } else if (exponent >= -16382 - 63) {
                // Number is subnormal
                f_cast.mantissa = mantissa >> (-16382 - exponent);
                f_cast.sign_exponent = 0;
            } else {
                // Exponent is too small, so we return zero
                goto return_zero;
            }
            if (sign)
                f_cast.sign_exponent |= 0x8000;
            *f_ptr = f_cast.f;
            return extra_chars;
        } else {
            got_digit = true;
            scanf_ungetc(file, offset, field_width, c);
        }
    } else {
        scanf_ungetc(file, offset, field_width, c);
    }
    // Skip initial zeroes
    bool got_decimal_point = false;
    ptrdiff_t decimal_point_position = 0;
    while (1) {
        c = scanf_char(file, offset, field_width);
        if (c == '0') {
            got_digit = true;
            if (got_decimal_point)
                decimal_point_position--;
        } else if (c == '.' && !got_decimal_point) {
            got_decimal_point = true;
        } else {
            scanf_ungetc(file, offset, field_width, c);
            break;
        }
    }
    // Buffer for storing groups of digits forming the integral part
    // Each element represents 19 decimal digits.
    // Must be long enough to hold log (2^(16383+63)) base (10^19) elements ≈ 260.56522 elements,
    // plus one at the end to account for normalization.
#define DEC_DIGIT_GROUPS_SIZE 262
    u64 dec_digit_groups[DEC_DIGIT_GROUPS_SIZE + 1];
    int dec_digit_groups_read_num = 0;
    // Read the digits
    while (1) {
        u64 digit_group = 0;
        for (int digit_i = 0; digit_i < 19; ) {
            c = scanf_char(file, offset, field_width);
            if ('0' <= c && c <= '9') {
                // Digit
                got_digit = true;
                if (!got_decimal_point)
                    decimal_point_position++;
                digit_group = 10 * digit_group + (c - '0');
                digit_i++;
            } else if (c == '.' && !got_decimal_point) {
                // Decimal point
                got_decimal_point = true;
            } else {
                // Unexpected character
                scanf_ungetc(file, offset, field_width, c);
                // Write last digit group
                if (digit_i != 0) {
                    for (int i = 0; i < 19 - digit_i; i++)
                        digit_group *= 10;
                    if (dec_digit_groups_read_num < DEC_DIGIT_GROUPS_SIZE)
                        dec_digit_groups[dec_digit_groups_read_num] = digit_group;
                    dec_digit_groups_read_num++;
                }
                goto end_of_digits;
            }
        }
        // Write next digit group
        if (dec_digit_groups_read_num < DEC_DIGIT_GROUPS_SIZE)
            dec_digit_groups[dec_digit_groups_read_num] = digit_group;
        dec_digit_groups_read_num++;
    }
end_of_digits:
    // Fail if we didn't find any digits
    if (!got_digit)
        return SIZE_MAX;
    // Read exponent
    c = scanf_char(file, offset, field_width);
    if (c == 'e' || c == 'E')
        decimal_point_position += scanf_exponent(file, offset, field_width, &extra_chars);
    else
        scanf_ungetc(file, offset, field_width, c);
    // Shift digits so that decimal point is within stored digit groups
    int dec_digit_groups_stored_num;
    int dec_digit_groups_fractional_start;
    int decimal_point_digit_i;
    if (decimal_point_position > DEC_DIGIT_GROUPS_SIZE * 19) {
        // The number is too big to fit, so we return infinity
        goto return_inf;
    } else if (decimal_point_position > 0) {
        // Number has nonzero integral part
        dec_digit_groups_stored_num = dec_digit_groups_read_num <= DEC_DIGIT_GROUPS_SIZE ? dec_digit_groups_read_num : DEC_DIGIT_GROUPS_SIZE;
        dec_digit_groups_fractional_start = decimal_point_position / 19;
        decimal_point_digit_i = decimal_point_position % 19;
    } else {
        // Number has no integral part, so we shift it so that the decimal point is at the start
        size_t copy_offset = (- decimal_point_position + 18) / 19;
        if (copy_offset > DEC_DIGIT_GROUPS_SIZE)
            copy_offset = DEC_DIGIT_GROUPS_SIZE;
        size_t copy_size = dec_digit_groups_read_num;
        if (copy_offset + copy_size > DEC_DIGIT_GROUPS_SIZE)
            copy_size = DEC_DIGIT_GROUPS_SIZE - copy_offset;
        memmove(dec_digit_groups + copy_offset, dec_digit_groups, copy_size * sizeof(u64));
        memset(dec_digit_groups, 0, copy_offset * sizeof(u64));
        dec_digit_groups_stored_num = copy_offset + copy_size;
        dec_digit_groups_fractional_start = 0;
        decimal_point_digit_i = (19 + decimal_point_position % 19) % 19;
    }
    // Normalize the digits to align decimal point with digit group boundary
    if (decimal_point_digit_i != 0) {
        // Divide the number by 10 ^ (19 - decimal_point_digit_i) in base 10^19
        u64 divisor = 1;
        for (int i = 0; i < 19 - decimal_point_digit_i; i++)
            divisor *= 10;
        u64 remainder = 0;
        for (int i = 0; i < dec_digit_groups_stored_num; i++) {
            // Set dec_digit_groups[i] = (remainder * POW_10_19 + dec_digit_groups[i]) / divisor
            // and remainder = (remainder * POW_10_19 + dec_digit_groups[i]) % divisor
            asm ("mul %[b]; add rax, %[l]; adc rdx, 0; div %[d]"
                : "=&a"(dec_digit_groups[i]), "=&d"(remainder)
                : [b] "r"(POW_10_19), [l] "r"(dec_digit_groups[i]), [d] "r"(divisor), "a"(remainder)
            );
        }
        // Store reimainder in new final digit
        for (int i = 0; i < decimal_point_digit_i; i++)
            remainder *= 10;
        dec_digit_groups[dec_digit_groups_stored_num] = remainder;
        dec_digit_groups_stored_num++;
        dec_digit_groups_fractional_start++;
    }
    // Covert the integral part into base 2^63
    // We only need the two highest digits to fill the mantissa.
    u64 mantissa_integral_digit_groups[2] = {0, 0};
    int mantissa_integral_digit_groups_num = 0;
    int dec_digit_groups_integral_num = dec_digit_groups_fractional_start;
    int first_dec_digit_group = 0;
    // Remove initial zeroes
    while (dec_digit_groups_integral_num > 0 && dec_digit_groups[first_dec_digit_group] == 0) {
        first_dec_digit_group++;
        dec_digit_groups_integral_num--;
    }
    while (dec_digit_groups_integral_num > 0) {
        // Divide the integral part by 2^63 in base 10^19
        u64 remainder = 0;
        for (int i = first_dec_digit_group; i < first_dec_digit_group + dec_digit_groups_integral_num; i++) {
            // Set dec_digit_groups[i] = (remainder * 10^19 + dec_digit_groups[i]) / 2^63
            // and remainder = (remainder * 10^19 + dec_digit_groups[i]) % 2^63
            u64 product_high, product_low;
            asm ("mul %[b]; add rax, %[l]; adc rdx, 0"
                : "=&d"(product_high), "=&a"(product_low)
                : [b] "r"(POW_10_19), [l] "r"(dec_digit_groups[i]), "a"(remainder)
            );
            dec_digit_groups[i] = (product_high << 1) | (product_low >> 63);
            remainder = product_low & (UINT64_C(-1) >> 1);
        }
        // The final remainder is the next digit
        mantissa_integral_digit_groups[1] = mantissa_integral_digit_groups[0];
        mantissa_integral_digit_groups[0] = remainder;
        mantissa_integral_digit_groups_num++;
        // If the highest digit group became zero, remove it
        if (dec_digit_groups[first_dec_digit_group] == 0) {
            first_dec_digit_group++;
            dec_digit_groups_integral_num--;
        }
    }
    // Assemble the result
    if (mantissa_integral_digit_groups_num == 0) {
        // The integral part is empty
        // Remove all trailing zeroes from the fractional part
        while (dec_digit_groups_stored_num > dec_digit_groups_fractional_start
                && dec_digit_groups[dec_digit_groups_stored_num - 1] == 0)
            dec_digit_groups_stored_num--;
        // If the fractional part is empty, the result is zero
        if (dec_digit_groups_fractional_start == dec_digit_groups_stored_num) {
            f_cast.mantissa = 0;
            f_cast.sign_exponent = 0;
            goto end;
        }
        // Covert the fractional part into base 2^63
        // We only need the two highest nonzero digits to fill the mantissa.
        u64 mantissa_fractional_digit_groups[2] = {0, 0};
        int mantissa_fractional_digit_groups_num = 0;
        int significant_digit_groups = 0;
        while (significant_digit_groups < 2) {
            // Multiply the fractional part by 2^63 in base 10^19
            u64 carry = 0;
            for (int i = dec_digit_groups_stored_num - 1; i >= dec_digit_groups_fractional_start; i--) {
                // Set dec_digit_groups[i] = (dec_digit_groups[i] * 2^63 + carry) % 10^19
                // and carry = (dec_digit_groups[i] * 2^63 + carry) / 10^19
                u64 product_high = dec_digit_groups[i] >> 1;
                u64 product_low = (dec_digit_groups[i] << 63) | carry;
                asm ("div %[b]"
                    : "=d"(dec_digit_groups[i]), "=a"(carry)
                    : "d"(product_high), "a"(product_low), [b] "r"(POW_10_19)
                );
            }
            // The final carry is the next digit
            // We move past all initial zero digits.
            if (significant_digit_groups > 0 || carry != 0) {
                mantissa_fractional_digit_groups[significant_digit_groups] = carry;
                significant_digit_groups++;
            } else {
                mantissa_fractional_digit_groups_num++;
            }
        }
        int leading_zeroes = __builtin_clzll(mantissa_fractional_digit_groups[0]);
        int exponent = - (mantissa_fractional_digit_groups_num * 63) - (leading_zeroes - 1) - 1;
        if (exponent < -16382 - 63) {
            // The exponent is too small to represent, so we return zero
return_zero:
            f_cast.mantissa = 0;
            f_cast.sign_exponent = 0;
        } else if (exponent < -16382) {
            // The number is subnormal
            f_cast.mantissa = mantissa_fractional_digit_groups[0] >> (-16382 - exponent);
            f_cast.sign_exponent = 0;
        } else {
            // The number is normal
            f_cast.mantissa = (mantissa_fractional_digit_groups[0] << leading_zeroes) | (mantissa_fractional_digit_groups[1] >> (63 - leading_zeroes));
            // Add bias to exponent
            f_cast.sign_exponent = exponent + 16383;
        }
    } else {
        // Convert the fractional part into base 2^63
        // We only need the highest digit to fill the mantissa.
        u64 mantissa_fractional_part;
        {
            // Multiply the fractional part by 2^63 in base 10^19 using
            u64 carry = 0;
            for (int i = dec_digit_groups_stored_num - 1; i >= dec_digit_groups_fractional_start; i--) {
                // Set carry = (dec_digit_groups[i] * 2^63 + carry) / 10^19
                u64 product_high = dec_digit_groups[i] >> 1;
                u64 product_low = (dec_digit_groups[i] << 63) | carry;
                asm ("div %[b]"
                    : "=a"(carry)
                    : "d"(product_high), "a"(product_low), [b] "r"(POW_10_19)
                );
            }
            // The final carry is the next digit
            mantissa_fractional_part = carry;
        }
        // Calculate exponent
        int leading_zeroes = __builtin_clzll(mantissa_integral_digit_groups[0]);
        int exponent = (mantissa_integral_digit_groups_num * 63) - (leading_zeroes - 1) - 1;
        if (exponent > 16383) {
            // The exponent is too large to represent, so we return infinity
return_inf:
            f_cast.mantissa = UINT64_C(1) << 63;
            f_cast.sign_exponent = 0x7FFF;
        } else if (exponent >= 63) {
            // The mantissa lies entirely within the integral part
            f_cast.mantissa = (mantissa_integral_digit_groups[0] << leading_zeroes) | (mantissa_integral_digit_groups[1] >> (63 - leading_zeroes));
            // Add bias to exponent
            f_cast.sign_exponent = exponent + 16383;
        } else {
            // The mantissa is split between the integral and fractional part
            f_cast.mantissa = (mantissa_integral_digit_groups[0] << leading_zeroes) | (mantissa_fractional_part >> (63 - leading_zeroes));
            // Add bias to exponent
            f_cast.sign_exponent = exponent + 16383;
        }
    }
end:
    if (sign)
        f_cast.sign_exponent |= 0x8000;
    *f_ptr = f_cast.f;
    return extra_chars;
}

static int scanf_common(FILE *file, const char *fmt, va_list args) {
    size_t offset = 0;
    int matches = 0;
    size_t i = 0;
    while (1) {
        // Check for end of format string
        if (fmt[i] == '\0')
            return matches;
        // For whitespace characters, match all available whitespace
        if (isspace(fmt[i])) {
            scanf_whitespace(file, &offset);
            i++;
            continue;
        }
        // If the next character is a normal character, try to match it
        if (fmt[i] != '%') {
            if (scanf_char(file, &offset, NULL) != fmt[i])
                return matches;
            i++;
            continue;
        }
        i++;
        // Check for assignment-suppressing *
        bool assign = true;
        if (fmt[i] == '*') {
            assign = false;
            i++;
        }
        // Read field width
        bool got_field_width = false;
        size_t field_width = 0;
        while ('0' <= fmt[i] && fmt[i] <= '9') {
            got_field_width = true;
            field_width = 10 * field_width + (fmt[i] - '0');
            i++;
        }
        if (!got_field_width)
            field_width = SIZE_MAX;
        // Read the length modifier
        LengthModifier length_modifier = read_length_modifier(fmt, &i);
        // Read conversion specifier
        char specifier = fmt[i];
        i++;
        switch (specifier) {
        case '%':
            if (length_modifier != LENGTH_MOD_NONE)
                return matches;
            if (scanf_char(file, &offset, &field_width) != '%')
                return matches;
            break;
        case 'c': {
            if (length_modifier != LENGTH_MOD_NONE)
                return matches;
            int c = scanf_char(file, &offset, &field_width);
            if (c == EOF)
                return matches;
            if (assign) {
                *va_arg(args, char *) = (char)c;
                matches++;
            }
            break;
        }
        case 's': {
            if (length_modifier != LENGTH_MOD_NONE)
                return matches;
            scanf_whitespace(file, &offset);
            char *s;
            if (assign)
                s = va_arg(args, char *);
            while (1) {
                int c = scanf_char(file, &offset, &field_width);
                if (c == EOF || isspace(c))
                    break;
                if (assign)
                    *s++ = (char)c;
            }
            if (assign) {
                *s = '\0';
                matches++;
            }
            break;
        }
        case '[': {
            if (length_modifier != LENGTH_MOD_NONE)
                return matches;
            // Parse character set
            bool negate = false;
            if (fmt[i] == '^') {
                negate = true;
                i++;
            }
            size_t group_start = i;
            if (fmt[i] == '\0')
                return matches;
            i++;
            while (fmt[i] != ']') {
                if (fmt[i] == '\0')
                    return matches;
                i++;
            }
            size_t group_end = i;
            i++;
            // Read string
            char *s;
            if (assign)
                s = va_arg(args, char *);
            while (1) {
                int c = scanf_char(file, &offset, &field_width);
                bool matched = false;
                for (size_t j = group_start; j < group_end; j++)
                    if (c == fmt[j])
                        matched = true;
                if (c == EOF || negate == matched)
                    break;
                if (assign)
                    *s++ = (char)c;
            }
            if (assign) {
                *s = '\0';
                matches++;
            }
            break;
        }
        case 'd':
        case 'i':
        case 'o':
        case 'x':
        case 'X':
        case 'u':
        case 'n': {
            uintmax_t n;
            if (specifier == 'n') {
                n = offset;
            } else {
                scanf_whitespace(file, &offset);
                int base;
                switch (specifier) {
                    case 'd':
                    case 'u':
                        base = 10;
                        break;
                    case 'i':
                        base = 0;
                        break;
                    case 'o':
                        base = 8;
                        break;
                    case 'x':
                    case 'X':
                        base = 16;
                        break;
                }
                if (!scanf_int(file, &offset, &field_width, &n, base))
                    return matches;
            }
            if (assign) {
                switch (length_modifier) {
                case LENGTH_MOD_hh:
                    *va_arg(args, unsigned char *) = (unsigned char)n;
                    break;
                case LENGTH_MOD_h:
                    *va_arg(args, unsigned short *) = (unsigned short)n;
                    break;
                case LENGTH_MOD_NONE:
                    *va_arg(args, unsigned int *) = (unsigned int)n;
                    break;
                case LENGTH_MOD_l:
                    *va_arg(args, unsigned long *) = (unsigned long)n;
                    break;
                case LENGTH_MOD_ll:
                    *va_arg(args, unsigned long long *) = (unsigned long long)n;
                    break;
                case LENGTH_MOD_j:
                    *va_arg(args, uintmax_t *) = n;
                    break;
                case LENGTH_MOD_z:
                    *va_arg(args, size_t *) = (size_t)n;
                    break;
                case LENGTH_MOD_t:
                    *va_arg(args, ptrdiff_t *) = (ptrdiff_t)n;
                    break;
                case LENGTH_MOD_L:
                    return matches;
                }
                matches++;
            }
            break;
        }
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A': {
            scanf_whitespace(file, &offset);
            long double f;
            if (__scanf_float(file, &offset, &field_width, &f) == SIZE_MAX)
                return matches;
            if (assign) {
                switch (length_modifier) {
                case LENGTH_MOD_NONE:
                    *va_arg(args, float *) = (float)f;
                    break;
                case LENGTH_MOD_l:
                    *va_arg(args, double *) = (double)f;
                    break;
                case LENGTH_MOD_L:
                    *va_arg(args, long double *) = (long double)f;
                    break;
                default:
                    return matches;
                }
                matches++;
            }
            break;
        }
        case 'p': {
            if (length_modifier != LENGTH_MOD_NONE)
                return matches;
            scanf_whitespace(file, &offset);
            uintmax_t n;
            if (!scanf_int(file, &offset, &field_width, &n, 16))
                return matches;
            if (assign) {
                *va_arg(args, void **) = (void *)n;
                matches++;
            }
            break;
        }
        // Incorrect specifiers have undefined behavior, so we choose to fail if we encounter one
        default:
            return matches;
        }
    }
}

int scanf(const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vscanf(format, args);
    va_end(args);
    return ret;
}

int fscanf(FILE *restrict f, const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vfscanf(f, format, args);
    va_end(args);
    return ret;
}

int sscanf(const char *restrict s, const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsscanf(s, format, args);
    va_end(args);
    return ret;
}

int vscanf(const char *restrict format, va_list args) {
    return vfscanf(stdin, format, args);
}

void __string_file(FILE *file, const char *s) {
    *file = (FILE){
        .type = FILE_BUFFER,
        .mode = FILE_R,
        .buffer = (char *restrict)s,
        .buffer_size = strlen(s),
        .buffer_offset = 0,
    };
}

int vsscanf(const char *restrict s, const char *restrict format, va_list args) {
    FILE file;
    __string_file(&file, s);
    return vfscanf(&file, format, args);
}

int vfscanf(FILE *restrict f, const char *restrict format, va_list args) {
    return scanf_common(f, format, args);
}

int feof(FILE *f) {
    return (int)f->eof;
}

int ferror(FILE *f) {
    return (int)f->error;
}

void clearerr(FILE *f) {
    f->eof = false;
    f->error = false;
}

int fflush(FILE *f) {
    err_t err;
    if (f->mode != FILE_W && f->mode != FILE_RW)
        return 0;
    if (f->buffer_mode == _IONBF)
        return 0;
    switch (f->type) {
    case FILE_INVALID:
    case FILE_BUFFER:
        break;
    case FILE_CHANNEL:
        err = channel_call(f->channel, &(SendMessage){1, &(SendMessageData){f->buffer_size, f->buffer}, 0, NULL}, NULL);
        f->buffer_size = 0;
        f->buffer_offset = 0;
        if (err)
            goto fail;
        break;
    }
    return 0;
fail:
    f->error = true;
    return EOF;
}

void setbuf(FILE *restrict f, char *restrict buf) {
    if (buf == NULL)
        setvbuf(f, NULL, _IONBF, 0);
    else
        setvbuf(f, buf, _IOFBF, BUFSIZ);
}

int setvbuf(FILE *restrict f, char *restrict buf, int mode, size_t size) {
    if (mode == _IONBF) {
        f->buffer_mode = _IONBF;
        return 0;
    }
    if (mode != _IOLBF && mode != _IOFBF)
        return 1;
    if (buf == NULL) {
        if (f->buffer_mode == _IONBF) {
            f->buffer = malloc(size);
            if (f->buffer == NULL)
                return 1;
        } else {
            char *new_buffer = realloc(f->buffer, size);
            if (new_buffer == NULL)
                return 1;
            f->buffer = new_buffer;
        }
    } else {
        if (f->buffer_mode != _IONBF)
            free(f->buffer);
        f->buffer = buf;
    }
    f->buffer_mode = mode;
    f->buffer_offset = 0;
    f->buffer_size = 0;
    f->buffer_capacity = size;
    return 0;
}
