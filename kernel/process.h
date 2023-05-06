#pragma once

#include "types.h"
#include "error.h"

#include "handle.h"
#include "spinlock.h"
#include "resource.h"

extern u8 tss[];
extern u8 tss_end[];

typedef struct FXSAVEArea FXSAVEArea;

typedef struct Process {
    void *rsp;
    void *kernel_stack;
    u64 page_map; // physical address of the PML4
    FXSAVEArea *fxsave_area;
    HandleList handles;
    ResourceList resources;
    struct Process *next_process;
} Process;

typedef struct ProcessQueue {
    Process *start;
    Process *end;
} ProcessQueue;

void process_queue_add(ProcessQueue *queue, Process *process);
Process *process_queue_remove(ProcessQueue *queue);
err_t process_create(Process **process_ptr, ResourceList resources);
void process_set_user_stack(Process *process, const u8 *file, size_t file_length);
void userspace_init(void);
void process_enqueue(Process *process);
err_t process_setup(void);
_Noreturn void process_exit(void);
void process_switch(void);
void sched_start(void);
void process_block(spinlock_t *spinlock);
