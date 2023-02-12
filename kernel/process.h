#pragma once

#include "types.h"

extern u8 tss[];
extern u8 tss_end[];

void userspace_init(void);
void spawn_process(u64 addr);
