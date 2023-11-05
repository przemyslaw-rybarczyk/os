#pragma once

#include "types.h"

i64 time_get(void);
void time_init(void);
void start_interrupt_timer(u64 tsc_deadline);
void disable_interrupt_timer(void);
bool tsc_past_deadline(void);
