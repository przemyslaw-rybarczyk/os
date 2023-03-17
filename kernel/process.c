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

static spinlock_t scheduler_lock;
static semaphore_t sched_queue_semaphore;

static ProcessQueueNode *process_queue_start = NULL;
static ProcessQueueNode *process_queue_end = NULL;

static void add_process_to_queue(ProcessQueueNode *pqn) {
    if (process_queue_start == NULL) {
        process_queue_start = pqn;
        process_queue_end = pqn;
    } else {
        process_queue_end->next = pqn;
        process_queue_end = pqn;
    }
    pqn->next = NULL;
}

static ProcessQueueNode *get_process_from_queue(void) {
    ProcessQueueNode *pqn = process_queue_start;
    process_queue_start = process_queue_start->next;
    return pqn;
}

// Create a new process and place it in the queue
// The arguments determine entry point and initial argument (passed in as RDI).
err_t spawn_process(const u8 *file, size_t file_length, u64 arg) {
    ProcessQueueNode *pqn = malloc(sizeof(ProcessQueueNode));
    if (pqn == NULL)
        return ERR_NO_MEMORY;
    // Allocate a process page map
    u64 page_map = page_alloc_clear();
    if (page_map == 0) {
        free(pqn);
        return ERR_NO_MEMORY;
    }
    pqn->process.page_map = page_map;
    // Copy the kernel mappings
    memcpy((u64 *)PHYS_ADDR(page_map) + 0x100, (u64 *)PHYS_ADDR(get_pml4()) + 0x100, 0x100 * 8);
    // Allocate a kernel stack
    pqn->process.kernel_stack = stack_alloc();
    if (pqn->process.kernel_stack == NULL) {
        page_free(pqn->process.page_map);
        free(pqn);
        return ERR_NO_MEMORY;
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
    add_process_to_queue(pqn);
    spinlock_release(&scheduler_lock);
    semaphore_increment(&sched_queue_semaphore);
    return 0;
}

// Set `cpu_local->current_process` to the next process in the queue
// The current process is not returned to the queue.
void sched_replace_process(void) {
    semaphore_decrement(&sched_queue_semaphore);
    spinlock_acquire(&scheduler_lock);
    cpu_local->current_process = get_process_from_queue();
    spinlock_release(&scheduler_lock);
}

// Return the current process to the end of the queue an set `cpu_local->current_process` to the next process in the queue
// The current scheduler is a basic round-robin scheduler.
void sched_switch_process(void) {
    spinlock_acquire(&scheduler_lock);
    // If there are no other processes to run, return to the current process
    if (process_queue_start == NULL) {
        spinlock_release(&scheduler_lock);
        return;
    }
    add_process_to_queue(cpu_local->current_process);
    cpu_local->current_process = get_process_from_queue();
    spinlock_release(&scheduler_lock);
}
