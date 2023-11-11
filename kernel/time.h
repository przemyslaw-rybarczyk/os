#pragma once

#include "types.h"

#include "error.h"

void time_init(void);
u64 time_get_tsc(void);
u64 time_from_tsc(u64 tsc);
u64 timestamp_to_tsc(i64 time);
i64 time_get(void);
bool tsc_past_deadline(void);
void schedule_timeslice_interrupt(u64 time);
void cancel_timeslice_interrupt(void);
void delayed_timer_interrupt_handle(void);
err_t syscall_process_wait(i64 time);
