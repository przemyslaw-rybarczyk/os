#include "types.h"
#include "process.h"

#include "alloc.h"
#include "page.h"
#include "percpu.h"
#include "segment.h"
#include "spinlock.h"
#include "stack.h"

#define RFLAGS_IF (1ull << 9)

typedef struct Process {
    void *rsp;
    void *kernel_stack;
} Process;

typedef struct ProcessQueueNode {
    Process process;
    struct ProcessQueueNode *next;
} ProcessQueueNode;

extern void process_initialize_state(Process *process, u64 entry, u64 arg, void *kernel_stack);
extern void jump_to_current_process(void);

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
bool spawn_process(u64 entry, u64 arg) {
    ProcessQueueNode *pqn = malloc(sizeof(ProcessQueueNode));
    if (pqn == NULL)
        return false;
    void *kernel_stack = stack_alloc();
    if (kernel_stack == NULL) {
        free(pqn);
        return false;
    }
    process_initialize_state(&pqn->process, entry, arg, kernel_stack);
    spinlock_acquire(&scheduler_lock);
    add_process_to_queue_end(pqn);
    spinlock_release(&scheduler_lock);
    return true;
}

// Same as schedule_next_process, but doesn't return the current process to the queue
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
