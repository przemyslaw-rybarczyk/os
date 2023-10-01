#pragma once

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

typedef struct _FILE FILE;

extern FILE *stdout;

#define stdout stdout

int putchar(int c);
int fputc(int c, FILE *f);
int puts(const char *s);
int fputs(const char *restrict s, FILE *restrict f);
int printf(const char *restrict format, ...);
int fprintf(FILE *restrict f, const char *restrict format, ...);
int sprintf(char *restrict buffer, const char *restrict format, ...);
int snprintf(char *restrict buffer, size_t size, const char *restrict format, ...);
int vprintf(const char *restrict format, va_list args);
int vfprintf(FILE *restrict f, const char *restrict format, va_list args);
int vsprintf(char *restrict buffer, const char *restrict format, va_list args);
int vsnprintf(char *restrict buffer, size_t size, const char *restrict format, va_list args);

#define putc(c, f) (fputc((c), (f)))
