#include "types.h"
#include "time.h"

#define STATUS_B_24_HOUR 2
#define STATUS_B_BINARY 4

struct rtc_time {
    u8 second;
    u8 minute;
    u8 hour;
    u8 day;
    u8 month;
    u8 year;
};

static u8 convert_from_bcd(u8 n) {
    return 10 * (n >> 4) + (n & 0x0F);
}

// Number of days in year before start of month
u16 month_offset[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

// Convert time in the RTC format to a timestamp
u64 convert_time_from_rtc(struct rtc_time rtc_time, u8 status_b) {
    // Check if highest bit of hour is set and clear it
    // Its value is needed if converting from 12-hour format later.
    bool hour_pm = rtc_time.hour & 0x80;
    rtc_time.hour &= 0x7F;
    // If time is in BCD, convert it to binary
    if (!(status_b & STATUS_B_BINARY)) {
        rtc_time.second = convert_from_bcd(rtc_time.second);
        rtc_time.minute = convert_from_bcd(rtc_time.minute);
        rtc_time.hour = convert_from_bcd(rtc_time.hour);
        rtc_time.day = convert_from_bcd(rtc_time.day);
        rtc_time.month = convert_from_bcd(rtc_time.month);
        rtc_time.year = convert_from_bcd(rtc_time.year);
    }
    // If hour is in 12-hour format, convert it to 24-hour format
    if (!(status_b & STATUS_B_24_HOUR)) {
        if (rtc_time.hour == 12)
            rtc_time.hour = 0;
        if (hour_pm)
            rtc_time.hour += 12;
    }
    // Assume year is in the range 2000-2099
    u64 year = 30 + rtc_time.year;
    // Days since epoch
    u64 day =
        year * 365 + (year + 1) / 4
        + month_offset[rtc_time.month - 1]
        + ((year % 4) == 0 && rtc_time.month > 2)
        + rtc_time.day - 1;
    // Seconds since epoch
    u64 second = rtc_time.second + 60 * (rtc_time.minute + 60 * (rtc_time.hour + 24 * day));
    return 10000000 * second;
}
