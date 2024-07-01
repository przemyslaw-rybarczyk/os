#include <time.h>

#include <stdlib.h>
#include <stdio.h>

#include <zr/error.h>
#include <zr/syscalls.h>
#include <zr/timezone.h>

#define TICKS_PER_SEC 10000000
#define NSEC_PER_TICK 100

static Timezone timezone = {0, DST_NONE};

Timezone timezone_get(void) {
    return timezone;
}

err_t timezone_set(Timezone timezone_) {
    if (timezone_.utc_offset < -95 || timezone_.utc_offset > 95)
        return ERR_INVALID_ARG;
    else if (timezone_.dst_type > DST_NA)
        return ERR_INVALID_ARG;
    timezone = timezone_;
    return 0;
}

void _time_init(void) {
    err_t err;
    // Try to read the timezone
    // On failure the default value of UTC is kept.
    Timezone new_timezone;
    err = message_resource_read(&resource_name("locale/timezone"), sizeof(Timezone), &new_timezone, -1, 0);
    if (!err)
        timezone_set(new_timezone);
}

// Division and modulo of 64-bit number, rounding down instead of towards zero

static i64 idiv(i64 t, i64 d) {
    if (t >= 0 || t % d == 0)
        return t / d;
    else
        return t / d - 1;
}

static i64 imod(i64 t, i64 d) {
    if (t >= 0 || t % d == 0)
        return t % d;
    else
        return t % d + d;
}

static void idivmod(i64 t, i64 d, i64 *quot, i64 *rem) {
    *quot = idiv(t, d);
    *rem = imod(t, d);
}

time_t time(time_t *t_ptr) {
    i64 t;
    time_get(&t);
    i64 sec;
    sec = idiv(t, TICKS_PER_SEC);
    if (t_ptr != NULL)
        *t_ptr = sec;
    return sec;
}

double difftime(time_t end, time_t start) {
    return (double)(end - start);
}

clock_t clock(void) {
    i64 t;
    process_time_get(&t);
    return t;
}

int timespec_get(struct timespec *ts, int base) {
    if (base != TIME_UTC)
        return 0;
    i64 t;
    time_get(&t);
    i64 sec, tick;
    idivmod(t, TICKS_PER_SEC, &sec, &tick);
    ts->tv_sec = sec;
    ts->tv_nsec = tick * NSEC_PER_TICK;
    return base;
}

// Length of each month
static u8 month_lengths[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// Number of days in year before start of month
static u16 month_offsets[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

struct tm *gmtime_r(const time_t *t_ptr, struct tm *tm) {
    i64 t = *t_ptr;
    // Calculate second, minute, and hour
    i64 sec, min, hour, day;
    idivmod(t, 60, &min, &sec);
    tm->tm_sec = sec;
    idivmod(min, 60, &hour, &min);
    tm->tm_min = min;
    idivmod(hour, 24, &day, &hour);
    tm->tm_hour = hour;
    // Calculate weekday
    tm->tm_wday = imod(day + 4, 7);
    // Now `day` contains the number of days since epoch.
    // Calculate the 400-, 100-, and 4-year leap year cycle number and the days' position within the year
    i64 year_400, year_100, year_4, year_1;
    idivmod(day + (369 * 365 + 89), 400 * 365 + 97, &year_400, &day);
    idivmod(day, 100 * 365 + 24, &year_100, &day);
    if (year_100 > 3)
        year_100 = 3;
    idivmod(day, 4 * 365 + 1, &year_4, &day);
    if (year_4 > 24)
        year_4 = 24;
    idivmod(day, 365, &year_1, &day);
    if (year_1 > 3)
        year_1 = 3;
    // Combine all cycles to get the year number (offset from 1601)
    i64 year = 400 * year_400 + 100 * year_100 + 4 * year_4 + year_1;
    tm->tm_year = year - 299;
    tm->tm_yday = day;
    // Calculate month and day of the month
    bool is_leap_year = year_1 == 3 && (year_4 != 24 || year_100 == 3);
    for (int i = 0; i < 12; i++) {
        i64 month_length = month_lengths[i] + (i == 1 && is_leap_year);
        i64 month_offset = month_offsets[i] + (i > 1 && is_leap_year);
        if (day - month_offset < month_length) {
            tm->tm_mday = day - month_offset + 1;
            tm->tm_mon = i;
            break;
        }
    }
    // DST is always off for UTC
    tm->tm_isdst = 0;
    return tm;
}

static bool year_is_leap(int tm_year) {
    // Years since 1600 (start of leap year cycle)
    i64 cyear = tm_year + 300;
    return (cyear % 4 == 0 && (cyear % 100 != 0 || cyear % 400 == 0));
}

// Shift the time by a given number of 15-minute intervals
// The shift value must be between -95 and 95.
// The date provided must be correctly formatted, or the behavior is undefined.
static void timezone_shift(struct tm *tm, int shift) {
    if (shift == 0)
        return;
    if (shift > 0) {
        // Add minutes
        tm->tm_min += 15 * (shift % 4);
        if (tm->tm_min >= 60) {
            tm->tm_min -= 60;
            tm->tm_hour++;
        }
        // Add hours
        tm->tm_hour += shift / 4;
        if (tm->tm_hour >= 24) {
            tm->tm_hour -= 24;
            tm->tm_mday++;
            tm->tm_yday++;
            tm->tm_wday = (tm->tm_wday + 1) % 7;
        }
        // Handle month and year overflow
        if (tm->tm_mday > month_lengths[tm->tm_mon]) {
            tm->tm_mday = 0;
            tm->tm_mon++;
            if (tm->tm_mon == 12) {
                tm->tm_yday = 0;
                tm->tm_year++;
            }
        }
    } else {
        // Add minutes
        tm->tm_min += 15 * (shift % 4);
        if (tm->tm_min < 0) {
            tm->tm_min += 60;
            tm->tm_hour--;
        }
        // Add hours
        tm->tm_hour += shift / 4;
        if (tm->tm_hour < 0) {
            tm->tm_hour += 24;
            tm->tm_mday--;
            tm->tm_yday--;
            tm->tm_wday = (tm->tm_wday + 6) % 7;
        }
        // Handle month and year overflow
        if (tm->tm_yday < 0) {
            tm->tm_year--;
            tm->tm_yday = 364 + year_is_leap(tm->tm_year);
            tm->tm_mon = 11;
            tm->tm_mday = 31;
        } else if (tm->tm_mday < 1) {
            tm->tm_mon--;
            tm->tm_mday = month_lengths[tm->tm_mon];
        }
    }
}

// Determines if European DST applies based on UTC time
static bool is_dst_eu(const struct tm *tm) {
    if (tm->tm_mon == 2) {
        // Mar - DST from last Sun at 01:00
        int next_sun = tm->tm_mday - tm->tm_wday + 7;
        if (next_sun > 31 && tm->tm_wday == 0)
            return tm->tm_hour >= 1;
        else if (next_sun > 31)
            return true;
        else
            return false;
    } else if (tm->tm_mon >= 3 && tm->tm_mon <= 8) {
        // Apr-Sep - DST
        return true;
    } else if (tm->tm_mon == 9) {
        // Oct - DST until last Sun at 01:00
        int next_sun = tm->tm_mday - tm->tm_wday + 7;
        if (next_sun > 31 && tm->tm_wday == 0)
            return tm->tm_hour < 1;
        else if (next_sun > 31)
            return false;
        else
            return true;
    } else {
        // Nov-Feb - no DST
        return false;
    }
}

// Determines if North American DST applies based on local time
static bool is_dst_na(const struct tm *tm) {
    if (tm->tm_mon == 2) {
        // Mar - DST from second Sun at 02:00
        int last_2_sun = tm->tm_mday - tm->tm_wday - 7;
        if (tm->tm_mday > 7 && tm->tm_mday <= 14 && tm->tm_wday == 0)
            return tm->tm_hour >= 2;
        else if (last_2_sun < 1)
            return false;
        else
            return true;
    } else if (tm->tm_mon >= 3 && tm->tm_mon <= 9) {
        // Apr-Oct - DST
        return true;
    } else if (tm->tm_mon == 10) {
        // Nov - DST until first Sun at 02:00
        int last_sun = tm->tm_mday - tm->tm_wday;
        if (tm->tm_mday <= 7 && tm->tm_wday == 0)
            return tm->tm_hour < 2;
        else if (last_sun < 1)
            return true;
        else
            return false;
    } else {
        // Dec-Feb - no DST
        return false;
    }
}

struct tm *localtime_r(const time_t *t_ptr, struct tm *tm) {
    if (!gmtime_r(t_ptr, tm))
        return NULL;
    if (timezone.dst_type == DST_EU && is_dst_eu(tm)) {
        timezone_shift(tm, 4);
        tm->tm_isdst = 1;
    }
    timezone_shift(tm, timezone.utc_offset);
    if (timezone.dst_type == DST_NA && is_dst_na(tm)) {
        timezone_shift(tm, 4);
        tm->tm_isdst = 1;
    }
    return tm;
}

time_t mktime(struct tm *tm) {
    int tm_isdst = tm->tm_isdst;
    // Handle month overflow
    i64 year_diff, month;
    idivmod(tm->tm_mon, 12, &year_diff, &month);
    tm->tm_mon = month;
    tm->tm_year += year_diff;
    // Years since epoch
    i64 year = tm->tm_year - 70;
    // Years since 1600 (start of leap year cycle)
    i64 cyear = tm->tm_year + 300;
    // True if year is leap
    bool is_leap_year = (cyear % 4 == 0 && (cyear % 100 != 0 || cyear % 400 == 0));
    // Leap years since epoch
    i64 leap_years = idiv(cyear, 4) - idiv(cyear, 100) + idiv(cyear, 400) - 89;
    // Days since epoch
    i64 day =
        year * 365 + leap_years
        + month_offsets[tm->tm_mon]
        - (is_leap_year && tm->tm_mon <= 1)
        + tm->tm_mday - 1;
    // Seconds since epoch
    time_t t = tm->tm_sec + 60 * (tm->tm_min + 60 * (tm->tm_hour + 24 * day));
    // Adjust all fields in tm struct
    gmtime_r(&t, tm);
    // Apply reverse timezone shift
    tm->tm_isdst = tm_isdst;
    t -= 15 * 60 * timezone.utc_offset;
    if (tm->tm_isdst < 0) {
        switch (timezone.dst_type) {
        case DST_NONE:
            tm->tm_isdst = 0;
            break;
        case DST_EU: {
            struct tm utc_tm = *tm;
            timezone_shift(&utc_tm, -timezone.utc_offset);
            tm->tm_isdst = is_dst_eu(&utc_tm);
            break;
        }
        case DST_NA:
            tm->tm_isdst = is_dst_na(tm);
            break;
        }
    }
    if (tm->tm_isdst > 0)
        t -= 60 * 60;
    return t;
}

static const char *month_name[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
static const char *wday_name[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

static int iso_week_of_the_year(const struct tm *tm) {
    i64 week = (tm->tm_yday - imod(tm->tm_wday - 1, 7) + 3 + 7) / 7;
    i64 this_year_starting_wday = imod(tm->tm_wday - tm->tm_yday, 7);
    bool this_year_has_leap_week = this_year_starting_wday == 4 || (this_year_starting_wday == 3 && year_is_leap(tm->tm_year));
    bool last_year_has_leap_week = this_year_starting_wday == 5 || (this_year_starting_wday == 6 && year_is_leap(tm->tm_year - 1));
    if (week < 1)
        return 52 + last_year_has_leap_week;
    else if (week > 52 + this_year_has_leap_week)
        return 1;
    else
        return week;
}

static int iso_week_based_year(const struct tm *tm) {
    i64 week = (tm->tm_yday - imod(tm->tm_wday - 1, 7) + 3 + 7) / 7;
    i64 this_year_starting_wday = imod(tm->tm_wday - tm->tm_yday, 7);
    bool this_year_has_leap_week = this_year_starting_wday == 4 || (this_year_starting_wday == 3 && year_is_leap(tm->tm_year));
    if (week < 1)
        return tm->tm_year - 1;
    else if (week > 52 + this_year_has_leap_week)
        return tm->tm_year + 1;
    else
        return tm->tm_year;
}

size_t strftime(char *restrict s, size_t s_size, const char *restrict fmt, const struct tm *restrict tm) {
    size_t offset = 0;
    size_t i = 0;
    while (fmt[i] != '\0' && offset < s_size) {
        // Normal character
        if (fmt[i] != '%') {
            s[offset++] = fmt[i++];
            continue;
        }
        i++;
        // Otherwise, we have a format specifier.
        // Read the modifier (E or O)
        if (fmt[i] == 'E' || fmt[i] == 'O')
            i++;
        // Read the main character of the specifier
        char *pstr = s + offset;
        size_t psize = s_size - offset;
        switch (fmt[i++]) {
        case '%':
            s[offset++] = '%';
            break;
        case 'n':
            s[offset++] = '\n';
            break;
        case 't':
            s[offset++] = '\t';
            break;
        case 'Y':
            offset += snprintf(pstr, psize, "%d", tm->tm_year + 1900);
            break;
        case 'y':
            offset += snprintf(pstr, psize, "%02d", tm->tm_year % 100);
            break;
        case 'C':
            offset += snprintf(pstr, psize, "%02d", tm->tm_year / 100 + 19);
            break;
        case 'G':
            offset += snprintf(pstr, psize, "%d", iso_week_based_year(tm) + 1900);
            break;
        case 'g':
            offset += snprintf(pstr, psize, "%02d", iso_week_based_year(tm) % 100);
            break;
        case 'b':
        case 'h':
            offset += snprintf(pstr, psize, "%.3s", tm->tm_mon >= 0 && tm->tm_mon < 12 ? month_name[tm->tm_mon] : "");
            break;
        case 'B':
            offset += snprintf(pstr, psize, "%s", tm->tm_mon >= 0 && tm->tm_mon < 12 ? month_name[tm->tm_mon] : "");
            break;
        case 'm':
            offset += snprintf(pstr, psize, "%02d", tm->tm_mon + 1);
            break;
        case 'U':
            offset += snprintf(pstr, psize, "%02d", (tm->tm_yday - tm->tm_wday + 7) / 7);
            break;
        case 'W':
            offset += snprintf(pstr, psize, "%02d", (tm->tm_yday - imod(tm->tm_wday - 1, 7) + 7) / 7);
            break;
        case 'V':
            offset += snprintf(pstr, psize, "%02d", iso_week_of_the_year(tm));
            break;
        case 'j':
            offset += snprintf(pstr, psize, "%03d", tm->tm_yday + 1);
            break;
        case 'd':
            offset += snprintf(pstr, psize, "%02d", tm->tm_mday);
            break;
        case 'e':
            offset += snprintf(pstr, psize, "%2d", tm->tm_mday);
            break;
        case 'a':
            offset += snprintf(pstr, psize, "%.3s", tm->tm_wday >= 0 && tm->tm_wday < 7 ? wday_name[tm->tm_wday] : "");
            break;
        case 'A':
            offset += snprintf(pstr, psize, "%s", tm->tm_wday >= 0 && tm->tm_wday < 7 ? wday_name[tm->tm_wday] : "");
            break;
        case 'w':
            offset += snprintf(pstr, psize, "%d", tm->tm_wday);
            break;
        case 'u':
            offset += snprintf(pstr, psize, "%d", tm->tm_wday == 0 ? 7 : tm->tm_wday);
            break;
        case 'H':
            offset += snprintf(pstr, psize, "%02d", tm->tm_hour);
            break;
        case 'I':
            offset += snprintf(pstr, psize, "%02d", tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12);
            break;
        case 'M':
            offset += snprintf(pstr, psize, "%02d", tm->tm_min);
            break;
        case 'S':
            offset += snprintf(pstr, psize, "%02d", tm->tm_sec);
            break;
        case 'c':
            offset += snprintf(pstr, psize, "%.3s %.3s %2d %02d:%02d:%02d %d",
                tm->tm_wday >= 0 && tm->tm_wday < 7 ? wday_name[tm->tm_wday] : "",
                tm->tm_mon >= 0 && tm->tm_mon < 12 ? month_name[tm->tm_mon] : "",
                tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_year + 1900);
            break;
        case 'x':
        case 'D':
            offset += snprintf(pstr, psize, "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100);
            break;
        case 'X':
        case 'T':
            offset += snprintf(pstr, psize, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
            break;
        case 'F':
            offset += snprintf(pstr, psize, "%d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
            break;
        case 'r':
            offset += snprintf(pstr, psize, "%02d:%02d:%02d %s",
                tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12,
                tm->tm_min, tm->tm_sec,
                tm->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'R':
            offset += snprintf(pstr, psize, "%02d:%02d", tm->tm_hour, tm->tm_min);
            break;
        case 'p':
            offset += snprintf(pstr, psize, "%s", tm->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'z':
        case 'Z': {
            int utc_offset = timezone.utc_offset + (tm->tm_isdst ? 4 : 0);
            offset += snprintf(pstr, psize, "%+03d%02d", utc_offset / 4, 15 * abs(utc_offset % 4));
            break;
        }
        default:
            break;
        }
    }
    if (offset >= s_size)
        return 0;
    s[offset] = '\0';
    return offset;
}
