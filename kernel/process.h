#pragma once

#include "types.h"

extern u8 tss[];
extern u8 tss_end[];

void userspace_init(void);
void jump_to_program(u64 addr);
