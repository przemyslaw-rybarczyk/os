#pragma once

#include <stddef.h>

typedef long long time_t;
typedef long long clock_t;

#define CLOCKS_PER_SEC 10000000LL

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

#define TIME_UTC 1

time_t time(time_t *t_ptr);
double difftime(time_t end, time_t start);
clock_t clock(void);
int timespec_get(struct timespec *ts, int base);
struct tm *gmtime_r(const time_t *t_ptr, struct tm *tm);
struct tm *localtime_r(const time_t *t_ptr, struct tm *tm);
time_t mktime(struct tm *tm);
size_t strftime(char *restrict s, size_t s_size, const char *restrict fmt, const struct tm *restrict tm);
