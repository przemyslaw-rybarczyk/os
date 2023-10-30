#include <stdlib.h>

#include <ctype.h>
#include <limits.h>
#include <stdint.h>

#include "file.h"

int abs(int n) {
    return n < 0 ? -n : n;
}

long labs(long n) {
    return n < 0 ? -n : n;
}

long long llabs(long long n) {
    return n < 0 ? -n : n;
}

div_t div(int x, int y) {
    return (div_t){.quot = x / y, .rem = x % y};
}

ldiv_t ldiv(long x, long y) {
    return (ldiv_t){.quot = x / y, .rem = x % y};
}

lldiv_t lldiv(long long x, long long y) {
    return (lldiv_t){.quot = x / y, .rem = x % y};
}

float strtof(const char *restrict str, char **restrict str_end) {
    return strtold(str, str_end);
}

double strtod(const char *restrict str, char **restrict str_end) {
    return strtold(str, str_end);
}

size_t __scanf_float(FILE *file, size_t *offset, size_t *field_width, long double *f_ptr);
void __string_file(FILE *file, const char *s);

long double strtold(const char *restrict str, char **restrict str_end) {
    // Skip whitespace
    size_t offset = 0;
    while (isspace(str[offset]))
        offset++;
    long double f;
    FILE file;
    __string_file(&file, str + offset);
    size_t field_width = SIZE_MAX;
    size_t extra_chars = __scanf_float(&file, &offset, &field_width, &f);
    if (extra_chars == SIZE_MAX) {
        if (str_end != NULL)
            *str_end = (char *)str;
        return 0.0;
    }
    if (str_end != NULL)
        *str_end = (char *)str + offset - extra_chars;
    return f;
}

// Returns the sign and overflow separately from the result
static unsigned long long strtoull_(const char *restrict str, char **restrict str_end, int base, bool *negate_ptr, bool *overflow_ptr) {
    size_t i = 0;
    // Skip whitespace
    while (isspace(str[i]))
        i++;
    // Check base is valid
    if (base < 0 || base == 1 || base > 36) {
        *negate_ptr = false;
        *overflow_ptr = false;
        return 0;
    }
    // Read sign
    bool negate;
    switch (str[i]) {
    case '+':
        negate = false;
        i++;
        break;
    case '-':
        negate = true;
        i++;
        break;
    default:
        negate = false;
        break;
    }
    // Read base prefix
    if (base == 0 || base == 16) {
        if (str[i] == '0') {
            if (str[i + 1] == 'x' || str[i + 1] == 'X') {
                i += 2;
                if (base == 0)
                    base = 16;
            } else if (base == 0) {
                base = 8;
            }
        } else if (base == 0) {
            base = 10;
        }
    }
    // Read digits
    bool overflow = false;
    bool has_digits = false;
    unsigned long long number = 0;
    while (1) {
        char c = str[i];
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
            if (has_digits) {
                if (str_end != NULL)
                    *str_end = (char *)str + i;
                *negate_ptr = negate;
                *overflow_ptr = overflow;
                return number;
            } else {
                if (str_end != NULL)
                    *str_end = (char *)str;
                *negate_ptr = false;
                *overflow_ptr = false;
                return 0;
            }
        }
        i++;
        has_digits = true;
        overflow = overflow || __builtin_umulll_overflow(number, base, &number);
        overflow = overflow || __builtin_uaddll_overflow(number, digit, &number);
    }
}

long strtol(const char *restrict str, char **restrict str_end, int base) {
    bool negate, overflow;
    unsigned long long n = strtoull_(str, str_end, base, &negate, &overflow);
    // Check for overflow
    if (negate) {
        if (overflow || n > -(unsigned long long)LONG_MIN)
            return LONG_MIN;
    } else {
        if (overflow || n > (unsigned long long)LONG_MAX)
            return LONG_MAX;
    }
    // Return value
    return negate ? -n : n;
}

long long strtoll(const char *restrict str, char **restrict str_end, int base) {
    bool negate, overflow;
    unsigned long long n = strtoull_(str, str_end, base, &negate, &overflow);
    // Check for overflow
    if (negate) {
        if (overflow || n > -(unsigned long long)LLONG_MIN)
            return LLONG_MIN;
    } else {
        if (overflow || n > (unsigned long long)LLONG_MAX)
            return LLONG_MAX;
    }
    // Return value
    return negate ? -n : n;
}

unsigned long strtoul(const char *restrict str, char **restrict str_end, int base) {
    bool negate, overflow;
    unsigned long long n = strtoull_(str, str_end, base, &negate, &overflow);
    // Check for overflow
    if (overflow || n > ULONG_MAX)
        return ULONG_MAX;
    // Return value
    return negate ? -n : n;
}

unsigned long long strtoull(const char *restrict str, char **restrict str_end, int base) {
    bool negate, overflow;
    unsigned long long n = strtoull_(str, str_end, base, &negate, &overflow);
    // Check for overflow
    if (overflow || n > ULLONG_MAX)
        return ULLONG_MAX;
    // Return value
    return negate ? -n : n;
}

double atof(const char *str) {
    return strtod(str, NULL);
}

int atoi(const char *str) {
    bool negate, overflow;
    unsigned long long n = strtoull_(str, NULL, 10, &negate, &overflow);
    // Check for overflow
    if (negate) {
        if (overflow || n > -(unsigned long long)INT_MIN)
            return INT_MIN;
    } else {
        if (overflow || n > (unsigned long long)INT_MAX)
            return INT_MAX;
    }
    // Return value
    return negate ? -n : n;
}

long atol(const char *str) {
    return strtol(str, NULL, 10);
}

long long atoll(const char *str) {
    return strtoll(str, NULL, 10);
}

void qsort(void *base_, size_t n, size_t size, int (*comp)(const void *, const void *)) {
    // Insertion sort
    u8 *base = base_;
    for (size_t i = 0; i < n; i++) {
        for (size_t j = i; j > 0; j--) {
            void *p1 = base + (j - 1) * size;
            void *p2 = base + j * size;
            if (comp(p1, p2) > 0) {
                // Swap elements at (j - 1) and j
                for (size_t si = 0; si < size / 8; si++) {
                    u64 t = ((u64 *)p1)[si];
                    ((u64 *)p1)[si] = ((u64 *)p2)[si];
                    ((u64 *)p2)[si] = t;
                }
                for (size_t si = (size / 8) * 8; si < size; si++) {
                    u8 t = ((u8 *)p1)[si];
                    ((u8 *)p1)[si] = ((u8 *)p2)[si];
                    ((u8 *)p2)[si] = t;
                }
            } else {
                break;
            }
        }
    }
}

void *bsearch(const void *key, const void *base_, size_t n, size_t size, int (*comp)(const void *, const void *)) {
    const u8 *base = base_;
    if (n == 0)
        return NULL;
    i64 lo = 0;
    i64 hi = n - 1;
    while (lo <= hi) {
        i64 i = (lo + hi) / 2;
        int cmp = comp(key, base + i * size);
        if (cmp > 0)
            lo = i + 1;
        else if (cmp < 0)
            hi = i - 1;
        else
            return (void *)(base + i * size);
    }
    return NULL;
}
