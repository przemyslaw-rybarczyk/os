#pragma once

#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void free(void *p);
void *realloc(void *p, size_t n);

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

int abs(int n);
long labs(long n);
long long llabs(long long n);
div_t div(int x, int y);
ldiv_t ldiv(long x, long y);
lldiv_t lldiv(long long x, long long y);

float strtof(const char *restrict str, char **restrict str_end);
double strtod(const char *restrict str, char **restrict str_end);
long double strtold(const char *restrict str, char **restrict str_end);

long strtol(const char *restrict str, char **restrict str_end, int base);
long long strtoll(const char *restrict str, char **restrict str_end, int base);
unsigned long strtoul(const char *restrict str, char **restrict str_end, int base);
unsigned long long strtoull(const char *restrict str, char **restrict str_end, int base);

double atof(const char *str);
int atoi(const char *str);
long atol(const char *str);
long long atoll(const char *str);

void qsort(void *base, size_t n, size_t size, int (*comp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*comp)(const void *, const void *));

#define RAND_MAX 2147483647

int rand(void);
void srand(unsigned int seed);
