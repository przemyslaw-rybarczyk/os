#pragma once

#include "types.h"

void time_init(void);
u64 time_from_tsc(u64 tsc);
u64 time_to_tsc(u64 tsc);
i64 time_get(void);
void start_interrupt_timer(u64 tsc_deadline);
void disable_interrupt_timer(void);
bool tsc_past_deadline(void);
