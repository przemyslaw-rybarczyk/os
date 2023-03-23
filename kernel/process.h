#pragma once

#include "types.h"
#include "error.h"

extern u8 tss[];
extern u8 tss_end[];

void userspace_init(void);
err_t process_spawn(const u8 *file, size_t file_length, u64 arg);
_Noreturn void process_exit(void);
void sched_yield(void);
void sched_start(void);
err_t syscall_message_get_length(size_t i, size_t *length);
err_t syscall_message_read(size_t i, void *data);
