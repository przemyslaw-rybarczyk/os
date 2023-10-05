#pragma once

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

typedef struct _FILE FILE;

extern FILE *stdout;
extern FILE *stderr;
extern FILE *stdin;

#define stdout stdout
#define stderr stderr
#define stdin stdin

int putchar(int c);
int getchar(void);
int fputc(int c, FILE *f);
int fgetc(FILE *f);
int puts(const char *s);
int fputs(const char *restrict s, FILE *restrict f);
char *fgets(char *restrict s, int n, FILE *restrict f);
int printf(const char *restrict format, ...);
int fprintf(FILE *restrict f, const char *restrict format, ...);
int sprintf(char *restrict buffer, const char *restrict format, ...);
int snprintf(char *restrict buffer, size_t size, const char *restrict format, ...);
int vprintf(const char *restrict format, va_list args);
int vfprintf(FILE *restrict f, const char *restrict format, va_list args);
int vsprintf(char *restrict buffer, const char *restrict format, va_list args);
int vsnprintf(char *restrict buffer, size_t size, const char *restrict format, va_list args);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);

#define putc(c, f) (fputc((c), (f)))
#define getc(f) (fgetc(f))
