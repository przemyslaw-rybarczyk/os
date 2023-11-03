#pragma once

#include <zr/types.h>

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
