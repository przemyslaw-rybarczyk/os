#pragma once

#include <zr/types.h>

#include <time.h>

#define TICKS_PER_SEC 10000000

typedef enum DSTType : u8 {
    DST_NONE,
    // DST from last Sunday in March at 01:00 UTC to last Sunday in October at 01:00 UTC
    DST_EU,
    // DST from second Sunday in March at 02:00 to first Sunday in November at 02:00
    DST_NA,
} DSTType;

typedef struct Timezone {
    // Offset from UTC in 15-minute intervals
    // Valid values are between -95 and 95.
    i8 utc_offset;
    // Type of DST used
    DSTType dst_type;
} Timezone;

Timezone timezone_get(void);
err_t timezone_set(Timezone timezone);

static inline time_t time_t_from_timestamp(i64 t) {
    if (t >= 0 || t % TICKS_PER_SEC == 0)
        return t / TICKS_PER_SEC;
    else
        return t / TICKS_PER_SEC - 1;
}
