#include "types.h"
#include "process.h"

#include "alloc.h"
#include "elf.h"
#include "page.h"
#include "percpu.h"
#include "segment.h"
#include "spinlock.h"
#include "stack.h"
#include "string.h"

#define RFLAGS_IF (1ull << 9)

typedef struct Process {
    void *rsp;
    void *kernel_stack;
    u64 page_map; // physical address of the PML4
} Process;

typedef struct ProcessQueueNode {
    Process process;
    struct ProcessQueueNode *next;
} ProcessQueueNode;

extern void jump_to_current_process(void);
extern u8 process_start[];

static spinlock_t scheduler_lock = SPINLOCK_FREE;

static ProcessQueueNode *process_queue_start = NULL;
static ProcessQueueNode *process_queue_end = NULL;

static void add_process_to_queue_end(ProcessQueueNode *pqn) {
    if (process_queue_end == NULL) {
        process_queue_start = pqn;
        process_queue_end = pqn;
    } else {
        process_queue_end->next = pqn;
        process_queue_end = pqn;
    }
    pqn->next = NULL;
}

// Create a new process and place it in the queue
// The arguments determine entry point and initial argument (passed in as RDI).
bool spawn_process(const u8 *file, size_t file_length, u64 arg) {
    ProcessQueueNode *pqn = malloc(sizeof(ProcessQueueNode));
    if (pqn == NULL)
        return false;
    // Allocate a process page map
    u64 page_map = page_alloc_clear();
    if (page_map == 0) {
        free(pqn);
        return false;
    }
    pqn->process.page_map = page_map;
    // Copy the kernel mappings
    memcpy((u64 *)PHYS_ADDR(page_map) + 0x100, (u64 *)PHYS_ADDR(get_pml4()) + 0x100, 0x100 * 8);
    // Allocate a kernel stack
    pqn->process.kernel_stack = stack_alloc();
    if (pqn->process.kernel_stack == NULL) {
        page_free(pqn->process.page_map);
        free(pqn);
        return false;
    }
    // Initialize the kernel stack contents
    u64 *rsp = pqn->process.kernel_stack;
    // Arguments to process_start()
    *--rsp = arg;
    *--rsp = file_length;
    *--rsp = (u64)file;
    // Used by sched_yield() - return address and saved registers
    // We set the return address to the entry point of process_start and zero all registers.
    *--rsp = (u64)process_start;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    pqn->process.rsp = rsp;
    // Add the process to the queue
    spinlock_acquire(&scheduler_lock);
    add_process_to_queue_end(pqn);
    spinlock_release(&scheduler_lock);
    return true;
}

// Same as schedule_next_process, but doesn't return the current process to the queue
// Called when initializing the scheduler.
void schedule_first_process(void) {
    spinlock_acquire(&scheduler_lock);
    cpu_local->current_process = process_queue_start;
    process_queue_start = process_queue_start->next;
    spinlock_release(&scheduler_lock);
}

// Set `cpu_local->current_process` to the next process in the queue
// The current scheduler is a basic round-robin scheduler.
// When this function is called the current process is returned to the end of the queue.
void schedule_next_process(void) {
    spinlock_acquire(&scheduler_lock);
    if (process_queue_start == NULL) {
        spinlock_release(&scheduler_lock);
        return;
    }
    add_process_to_queue_end(cpu_local->current_process);
    cpu_local->current_process = process_queue_start;
    process_queue_start = process_queue_start->next;
    spinlock_release(&scheduler_lock);
}
