#pragma once

#include "types.h"
#include "error.h"

#include "handle.h"
#include "spinlock.h"

extern u8 tss[];
extern u8 tss_end[];

typedef struct Process Process;

typedef struct ProcessQueue {
    Process *start;
    Process *end;
} ProcessQueue;

void process_queue_add(ProcessQueue *queue, Process *process);
Process *process_queue_remove(ProcessQueue *queue);
void userspace_init(void);
err_t process_enqueue(Process *process);
err_t process_setup(void);
_Noreturn void process_exit(void);
void sched_yield(void);
void sched_start(void);
void process_block(spinlock_t *spinlock);
err_t process_get_handle(size_t i, Handle *handle);
err_t process_add_handle(Handle handle, size_t *i_ptr);
void process_clear_handle(size_t i);
