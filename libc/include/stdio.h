#pragma once

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

typedef struct __FILE FILE;

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
int ungetc(int c, FILE *f);
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
int scanf(const char *restrict format, ...);
int fscanf(FILE *restrict f, const char *restrict format, ...);
int sscanf(const char *restrict s, const char *restrict format, ...);
int vscanf(const char *restrict format, va_list args);
int vfscanf(FILE *restrict f, const char *restrict format, va_list args);
int vsscanf(const char *restrict s, const char *restrict format, va_list args);
int feof(FILE *f);
int ferror(FILE *f);
void clearerr(FILE *f);
int fflush(FILE *f);
void setbuf(FILE *restrict f, char *restrict buf);
int setvbuf(FILE *restrict f, char *restrict buf, int mode, size_t size);

#define putc(c, f) (fputc((c), (f)))
#define getc(f) (fgetc(f))

#define _IONBF 0
#define _IOLBF 1
#define _IOFBF 2

#define BUFSIZ 4096
