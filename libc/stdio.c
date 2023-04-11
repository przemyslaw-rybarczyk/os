#include "types.h"
#include <stdio.h>

#include <stdarg.h>

#include <syscalls.h>

int putchar(int c) {
    c = (unsigned char)c;
    print_char(c);
    return c;
}

int puts(const char *s) {
    for (const char *p = s; *p != '\0'; p++)
        print_char(*p);
    return 0;
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
        print_char(c);
    } else {
        // Write a byte to a buffer if it's within bounds
        if (*(target.offset) < target.size) {
            target.buffer[*(target.offset)] = c;
        }
    }
    (*(target.offset))++;
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
            for (const char *p = s; *p != '\0'; p++)
                printf_char(target, *p);
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
