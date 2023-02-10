#pragma once

#include "types.h"

#define SEGMENT_KERNEL_CODE 0x08
#define SEGMENT_KERNEL_DATA 0x10
#define SEGMENT_USER_CODE 0x18
#define SEGMENT_USER_DATA 0x20

void gdt_init(void);
