#include "types.h"
#include "process.h"

#include "alloc.h"
#include "page.h"
#include "segment.h"
#include "string.h"

#define RFLAGS_IF (1ull << 9)

extern void jump_to_current_process(void);

typedef struct RegisterState {
    u64 rax;
    u64 rcx;
    u64 rdx;
    u64 rbx;
    u64 rbp;
    u64 rsp;
    u64 rsi;
    u64 rdi;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rip;
    u64 rflags;
    u64 cs;
    u64 ss;
} RegisterState;

typedef struct Process {
    RegisterState registers;
    u64 kernel_stack_phys;
} Process;

typedef struct ProcessQueueNode {
    Process process;
    struct ProcessQueueNode *next;
} ProcessQueueNode;

ProcessQueueNode *current_process = NULL;
static ProcessQueueNode *process_queue_end = NULL;

static void add_process_to_queue_end(ProcessQueueNode *pqn) {
    if (process_queue_end == NULL) {
        current_process = pqn;
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
    memset(pqn, 0, sizeof(ProcessQueueNode));
    pqn->process.registers.rdi = arg;
    pqn->process.registers.rip = entry;
    pqn->process.registers.rflags = RFLAGS_IF;
    pqn->process.registers.cs = SEGMENT_USER_CODE | SEGMENT_RING_3;
    pqn->process.registers.ss = SEGMENT_USER_DATA | SEGMENT_RING_3;
    pqn->process.kernel_stack_phys = page_alloc();
    if (pqn->process.kernel_stack_phys == 0)
        return false;
    add_process_to_queue_end(pqn);
    return true;
}

// Set `current_process` to the next process in the queue
// The current scheduler is a basic round-robin scheduler.
// When this function is called the current process is returned to the end of the queue.
void schedule_next_process(void) {
    if (current_process->next == NULL) {
        return;
    }
    ProcessQueueNode *next_pqn = current_process->next;
    add_process_to_queue_end(current_process);
    current_process = next_pqn;
}
