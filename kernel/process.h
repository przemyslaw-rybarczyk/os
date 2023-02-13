#pragma once

#include "types.h"

extern u8 tss[];
extern u8 tss_end[];

void userspace_init(void);
bool spawn_process(u64 addr, u64 arg);
void sched_yield(void);
void sched_start(void);
