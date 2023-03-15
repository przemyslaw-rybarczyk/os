#pragma once

#include "types.h"
#include "error.h"

#define SEGMENT_KERNEL_CODE 0x08
#define SEGMENT_KERNEL_DATA 0x10
#define SEGMENT_USER_DATA 0x18
#define SEGMENT_USER_CODE 0x20
#define TSS_DESCRIPTOR 0x28

#define SEGMENT_RING_3 0x03

err_t gdt_init(void);
