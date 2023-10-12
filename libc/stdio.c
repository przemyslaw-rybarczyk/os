#include <zr/types.h>
#include <stdio.h>

#include <ctype.h>
#include <float.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <zr/syscalls.h>

typedef enum FileType {
    FILE_INVALID,
    FILE_BUFFER,
    FILE_CHANNEL,
} FileType;

typedef enum FileMode {
    FILE_R,
    FILE_W,
    FILE_RW,
} FileMode;

#undef _IONBF
#undef _IOLBF
#undef _IOFBF

typedef enum BufferMode {
    _IONBF,
    _IOLBF,
    _IOFBF,
} BufferMode;

struct _FILE {
    FileType type;
    FileMode mode;
    BufferMode buffer_mode;
    char *restrict buffer;
    size_t buffer_capacity;
    size_t buffer_size;
    size_t buffer_offset;
    handle_t channel;
    bool eof;
    bool error;
    bool ungetc_buffer_full;
    unsigned char ungetc_buffer;
};

static FILE stdout_file = (FILE){.type = FILE_INVALID, .mode = FILE_W};
static FILE stderr_file = (FILE){.type = FILE_INVALID, .mode = FILE_W};
static FILE stdin_file = (FILE){.type = FILE_INVALID, .mode = FILE_R};

FILE *stdout = &stdout_file;
FILE *stderr = &stderr_file;
FILE *stdin = &stdin_file;

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

// Print a null-terminated string
static void printf_string(FILE *file, size_t *offset, const char *s) {
    for (const char *p = s; *p != '\0'; p++)
        printf_char(file, offset, *p);
}

// Print an unsinged decimal number
static void printf_dec(FILE *file, size_t *offset, uintmax_t n) {
    if (n == 0) {
        printf_char(file, offset, '0');
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
        printf_char(file, offset, digits[i - j - 1]);
}

// Print a singed decimal number
static void printf_dec_signed(FILE *file, size_t *offset, intmax_t n) {
    uintmax_t nu; // Absolute value of n
    // If the number is negative, print a minus sign and invert it
    if (n < 0) {
        printf_char(file, offset, '-');
        nu = -n;
    } else {
        nu = n;
    }
    // Print the number without the sign
    printf_dec(file, offset, nu);
}

// Print an unsinged octal number
static void printf_oct(FILE *file, size_t *offset, uintmax_t n) {
    if (n == 0) {
        printf_char(file, offset, '0');
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
        printf_char(file, offset, digit + '0');
    }
}

// Print an unsinged hexadecimal number
// `uppercase` determines whether digits above 9 are printed as "ABCDEF" or "abcdef".
static void printf_hex(FILE *file, size_t *offset, uintmax_t n, bool uppercase) {
    if (n == 0) {
        printf_char(file, offset, '0');
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
        printf_char(file, offset, digit < 10 ? digit + '0' : digit - 10 + (uppercase ? 'A' : 'a'));
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
static void printf_float(FILE *file, size_t *offset, long double f, bool uppercase) {
    // Extract mantissa and exponent fields
    union long_double_cast f_cast;
    f_cast.ld = f;
    u64 mantissa = f_cast.mantissa;
    u16 exponent_field = f_cast.sign_exponent & 0x7FFF;
    // If the sign bit is set, print a minus sign
    if (f_cast.sign_exponent & 0x8000)
        printf_char(file, offset, '-');
    // Handle NaN and infinity
    if (exponent_field == 0x7FFF) {
        // When checking the mantissa, ignore the highest bit, since it should be 1
        if ((mantissa & 0x7FFFFFFFFFFFFFFF) == 0)
            printf_string(file, offset, uppercase ? "INF" : "inf");
        else
            printf_string(file, offset, uppercase ? "NAN" : "nan");
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
    if (dec_digit_groups_num == 0) {
        // If the integral part is zero, print a zero
        printf_char(file, offset, '0');
    } else {
        // Print the first digit group while skipping initial zeroes
        size_t n = dec_digit_groups[dec_digit_groups_num - 1];
        int i = 0;
        for (; n > 0; n /= 10)
            dec_digits[i++] = (n % 10) + '0';
        for (int j = 0; j < i; j++)
            printf_char(file, offset, dec_digits[i - j - 1]);
        // Print the remaining digit groups
        for (int i = dec_digit_groups_num - 2; i >= 0; i--) {
            u64 n = dec_digit_groups[i];
            for (int j = 0; j < 19; j++) {
                dec_digits[j] = (n % 10) + '0';
                n /= 10;
            }
            for (int j = 0; j < 19; j++)
                printf_char(file, offset, dec_digits[18 - j]);
        }
    }
    // Print the decimal point
    printf_char(file, offset, '.');
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
    // Multiply the fractional part by 10^19 to get the highest 19 digits
    u64 remainder = 0;
    for (int i = fp_digits_num - 1; i >= 0; i--) {
        // Set remainder:fp_digits[i] = fp_digits[i] * POW_10_19 + remainder (":" indicates concatenation)
        asm ("mul %[m]; add rax, %[r]; adc rdx, 0"
            : "=&d"(remainder), "=&a"(fp_digits[i])
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
        printf_char(file, offset, dec_digits[18 - j]);
}

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
        // Previous character was '%', so the next character is a conversion specifier
        switch (fmt[i++]) {
        case '%':
            printf_char(file, offset, '%');
            break;
        case 'c': {
            int c = va_arg(args, int);
            printf_char(file, offset, (unsigned char)c);
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            printf_string(file, offset, s);
            break;
        }
        case 'd':
        case 'i': {
            int n = va_arg(args, int);
            printf_dec_signed(file, offset, n);
            break;
        }
        case 'o': {
            unsigned int n = va_arg(args, unsigned int);
            printf_oct(file, offset, n);
            break;
        }
        case 'x': {
            unsigned int n = va_arg(args, unsigned int);
            printf_hex(file, offset, n, false);
            break;
        }
        case 'X': {
            unsigned int n = va_arg(args, unsigned int);
            printf_hex(file, offset, n, true);
            break;
        }
        case 'u': {
            unsigned int n = va_arg(args, unsigned int);
            printf_dec(file, offset, n);
            break;
        }
        case 'f': {
            double f = va_arg(args, double);
            printf_float(file, offset, f, false);
            break;
        }
        case 'F': {
            double f = va_arg(args, double);
            printf_float(file, offset, f, true);
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

static void scanf_whitespace(FILE *file) {
    while (1) {
        int c = fgetc(file);
        if (!isspace(c)) {
            ungetc(c, file);
            break;
        }
    }
}

static bool scanf_int_unsigned(FILE *file, uintmax_t *n_ptr, int base) {
    int c;
    // Read sign
    bool negate;
    c = fgetc(file);
    switch (c) {
    case '+':
        negate = false;
        break;
    case '-':
        negate = true;
        break;
    default:
        negate = false;
        ungetc(c, file);
        break;
    }
    // Read base prefix
    bool has_digits = false;
    if (base == 0 || base == 8 || base == 16) {
        c = fgetc(file);
        if (c == '0') {
            if (base == 0 || base == 16) {
                c = fgetc(file);
                if (c == 'x' || c == 'X') {
                    if (base == 0)
                        base = 16;
                } else {
                    ungetc(c, file);
                    if (base == 0)
                        base = 8;
                    has_digits = true;
                }
            } else {
                has_digits = true;
            }
        } else {
            ungetc(c, file);
            if (base == 0)
                base = 10;
        }
    }
    // Read digits
    uintmax_t number = 0;
    while (1) {
        c = fgetc(file);
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
            ungetc(c, file);
            if (has_digits)
                *n_ptr = negate ? -number : number;
            return has_digits;
        }
        number = base * number + digit;
        has_digits = true;
    }
}

static bool scanf_int_signed(FILE *file, intmax_t *n_ptr, int base) {
    uintmax_t un;
    if (!scanf_int_unsigned(file, &un, base))
        return false;
    *n_ptr = (intmax_t)un;
    return true;
}

static bool scanf_float(FILE *file, long double *f_ptr) {
    int c;
    union long_double_cast f_cast;
    // Read sign
    bool sign;
    c = fgetc(file);
    switch (c) {
    case '+':
        sign = false;
        break;
    case '-':
        sign = true;
        break;
    default:
        sign = false;
        ungetc(c, file);
        break;
    }
    // Check first character after sign
    c = fgetc(file);
    if (tolower(c) == 'i') {
        // Parse infinity
        for (const char *s = "nf"; *s != '\0'; s++) {
            c = fgetc(file);
            if (tolower(c) != *s) {
                ungetc(c, file);
                return false;
            }
        }
        for (const char *s = "inity"; *s != '\0'; s++) {
            c = fgetc(file);
            if (tolower(c) != *s) {
                ungetc(c, file);
                goto return_inf;
            }
        }
        goto return_inf;
    } else if (tolower(c) == 'n') {
        // Parse NaN
        for (const char *s = "an"; *s != '\0'; s++) {
            c = fgetc(file);
            if (tolower(c) != *s) {
                ungetc(c, file);
                return false;
            }
        }
        c = fgetc(file);
        if (c == '(') {
            while (1) {
                c = fgetc(file);
                if (c == ')') {
                    break;
                } else if (!(isalnum(c) || c == '_')) {
                    ungetc(c, file);
                    break;
                }
            }
        } else {
            ungetc(c, file);
        }
        f_cast.mantissa = UINT64_C(-1);
        f_cast.sign_exponent = sign ? 0xFFFF : 0x7FFF;
        *f_ptr = f_cast.ld;
        return true;
    }
    ungetc(c, file);
    // Skip initial zeroes
    bool got_digit = false;
    while (1) {
        c = fgetc(file);
        if (c != '0') {
            ungetc(c, file);
            break;
        } else {
            got_digit = true;
        }
    }
    // Buffer for storing groups of digits forming the integral part
    // Each element represents 19 decimal digits.
    // Must be long enough to hold log (2^(16383+63)) base (10^19) elements ≈ 260.56522 elements,
    // plus one at the end to account for normalization.
#define DEC_DIGIT_GROUPS_SIZE 262
    u64 dec_digit_groups[DEC_DIGIT_GROUPS_SIZE + 1];
    int dec_digit_groups_stored_num = 0;
    // Read the digits
    bool got_decimal_point = false;
    int dec_digit_groups_integral_num = 0;
    int decimal_point_digit_i;
    while (1) {
        u64 digit_group = 0;
        for (int digit_i = 0; digit_i < 19; ) {
            c = fgetc(file);
            if ('0' <= c && c <= '9') {
                // Digit
                got_digit = true;
                digit_group = 10 * digit_group + (c - '0');
                digit_i++;
            } else if (c == '.' && !got_decimal_point) {
                // Decimal point
                got_decimal_point = true;
                dec_digit_groups_integral_num = dec_digit_groups_stored_num;
                decimal_point_digit_i = digit_i;
            } else {
                // Unexpected character
                ungetc(c, file);
                // Place decimal point at end if we didn't get one yet
                if (!got_decimal_point) {
                    dec_digit_groups_integral_num = dec_digit_groups_stored_num;
                    decimal_point_digit_i = digit_i;
                }
                // Write last digit group
                if (digit_i != 0) {
                    for (int i = 0; i < 19 - digit_i; i++)
                        digit_group *= 10;
                    if (dec_digit_groups_stored_num < DEC_DIGIT_GROUPS_SIZE) {
                        dec_digit_groups[dec_digit_groups_stored_num] = digit_group;
                        dec_digit_groups_stored_num++;
                    } else if (!got_decimal_point) {
                        goto return_inf;
                    }
                }
                goto end_of_digits;
            }
        }
        // Write next digit group
        if (dec_digit_groups_stored_num < DEC_DIGIT_GROUPS_SIZE) {
            dec_digit_groups[dec_digit_groups_stored_num] = digit_group;
            dec_digit_groups_stored_num++;
        } else if (!got_decimal_point) {
            // If there are too many digits before the decimal point to fit, return infinity
            goto return_inf;
        }
    }
end_of_digits:
    // Fail if we didn't find any digits
    if (!got_digit)
        return false;
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
        for (int i = 0; i < decimal_point_digit_i; i++)
            remainder *= 10;
        dec_digit_groups[dec_digit_groups_stored_num] = remainder;
        dec_digit_groups_stored_num++;
        dec_digit_groups_integral_num++;
    }
    int dec_digit_groups_fractional_start = dec_digit_groups_integral_num;
    // Covert the integral part into base 2^63
    // We only need the two highest digits to fill the mantissa.
    u64 mantissa_integral_digit_groups[2] = {0, 0};
    int mantissa_integral_digit_groups_num = 0;
    int first_dec_digit_group = 0;
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
            for (int i = dec_digit_groups_fractional_start; i < dec_digit_groups_stored_num; i++) {
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
    *f_ptr = f_cast.ld;
    return true;
}

static int scanf_common(FILE *file, const char *fmt, va_list args) {
    int matches = 0;
    size_t i = 0;
    while (1) {
        // Check for end of format string
        if (fmt[i] == '\0')
            return matches;
        // For whitespace characters, match all available whitespace
        if (isspace(fmt[i])) {
            scanf_whitespace(file);
            i++;
            continue;
        }
        // If the next character is a normal character, try to match it
        if (fmt[i] != '%') {
            if (fgetc(file) != fmt[i])
                return matches;
            i++;
            continue;
        }
        i++;
        // Previous character was '%', so the next character is a conversion specifier
        switch (fmt[i++]) {
        case '%':
            if (fgetc(file) != '%')
                return matches;
            break;
        case 'c': {
            char *c_ptr = va_arg(args, char *);
            int c = fgetc(file);
            if (c == EOF)
                return matches;
            *c_ptr = (char)c;
            matches++;
            break;
        }
        case 's': {
            scanf_whitespace(file);
            char *s = va_arg(args, char *);
            while (1) {
                int c = fgetc(file);
                if (c == EOF || isspace(c))
                    break;
                *s++ = (char)c;
            }
            *s = '\0';
            matches++;
            break;
        }
        case 'd': {
            scanf_whitespace(file);
            int *n_ptr = va_arg(args, int *);
            intmax_t n;
            if (!scanf_int_signed(file, &n, 10))
                return matches;
            *n_ptr = (int)n;
            matches++;
            break;
        }
        case 'i': {
            scanf_whitespace(file);
            int *n_ptr = va_arg(args, int *);
            intmax_t n;
            if (!scanf_int_signed(file, &n, 0))
                return matches;
            *n_ptr = (int)n;
            matches++;
            break;
        }
        case 'o': {
            scanf_whitespace(file);
            unsigned int *n_ptr = va_arg(args, unsigned int *);
            uintmax_t n;
            if (!scanf_int_unsigned(file, &n, 8))
                return matches;
            *n_ptr = (unsigned int)n;
            matches++;
            break;
        }
        case 'x':
        case 'X': {
            scanf_whitespace(file);
            unsigned int *n_ptr = va_arg(args, unsigned int *);
            uintmax_t n;
            if (!scanf_int_unsigned(file, &n, 16))
                return matches;
            *n_ptr = (unsigned int)n;
            matches++;
            break;
        }
        case 'u': {
            scanf_whitespace(file);
            unsigned int *n_ptr = va_arg(args, unsigned int *);
            uintmax_t n;
            if (!scanf_int_unsigned(file, &n, 10))
                return matches;
            *n_ptr = (unsigned int)n;
            matches++;
            break;
        }
        case 'f':
        case 'F': {
            scanf_whitespace(file);
            float *f_ptr = va_arg(args, float *);
            long double f;
            if (!scanf_float(file, &f))
                return matches;
            *f_ptr = (float)f;
            matches++;
            break;
        }
        // Incorrect specifiers have undefined behavior, so we choose to ignore them
        case '\0':
            return matches;
        default:
            break;
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

int vsscanf(const char *restrict s, const char *restrict format, va_list args) {
    FILE file = {
        .type = FILE_BUFFER,
        .buffer = (char *restrict)s,
        .buffer_size = strlen(s),
        .buffer_offset = 0,
    };
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
