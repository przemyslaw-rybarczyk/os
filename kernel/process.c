#include "types.h"
#include "process.h"

#include "alloc.h"
#include "elf.h"
#include "handle.h"
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
    HandleList handles;
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
err_t process_spawn(const u8 *file, size_t file_length, u64 arg) {
    err_t err;
    ProcessQueueNode *pqn = malloc(sizeof(ProcessQueueNode));
    if (pqn == NULL) {
        err = ERR_NO_MEMORY;
        goto fail_pqn_alloc;
    }
    // Allocate a process page map
    u64 page_map = page_alloc_clear();
    if (page_map == 0) {
        err = ERR_NO_MEMORY;
        goto fail_page_map_alloc;
    }
    pqn->process.page_map = page_map;
    // Copy the kernel mappings
    memcpy((u64 *)PHYS_ADDR(page_map) + 0x100, (u64 *)PHYS_ADDR(get_pml4()) + 0x100, 0x100 * 8);
    // Allocate a kernel stack
    pqn->process.kernel_stack = stack_alloc();
    if (pqn->process.kernel_stack == NULL) {
        err = ERR_NO_MEMORY;
        goto fail_stack_alloc;
    }
    // Initialize the argument message
    Message *message = malloc(sizeof(Message));
    if (message == NULL) {
        err = ERR_NO_MEMORY;
        goto fail_message_alloc;
    }
    message->data_size = sizeof(u64);
    message->data = malloc(message->data_size);
    if (message->data == NULL) {
        err = ERR_NO_MEMORY;
        goto fail_message_data_alloc;
    }
    memcpy(message->data, &arg, message->data_size);
    // Initialize the handle list
    err = handle_list_init(&pqn->process.handles);
    if (err)
        goto fail_handle_list_init;
    err = handle_add(&pqn->process.handles, (Handle){HANDLE_TYPE_MESSAGE, {.message = message}}, NULL);
    if (err)
        goto fail_handle_add;
    // Initialize the kernel stack contents
    u64 *rsp = pqn->process.kernel_stack;
    // Arguments to process_start()
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
fail_handle_add:
    handle_list_free(&pqn->process.handles);
fail_handle_list_init:
    free(message->data);
fail_message_data_alloc:
    free(message);
fail_message_alloc:
    stack_free(pqn->process.kernel_stack);
fail_stack_alloc:
    page_free(pqn->process.page_map);
fail_page_map_alloc:
    free(pqn);
fail_pqn_alloc:
    return err;
}

// Free the current process
// Does not free any information that is necessary to switch to the process when it's running in kernel mode,
// as it needs to be freed separately and with interrupts disabled.
void process_free_contents(void) {
    page_map_free_contents(cpu_local->current_process->process.page_map);
    handle_list_free(&cpu_local->current_process->process.handles);
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

// Verify that a buffer provided by a process is contained within the process address space
// This does not handle the cases where an address is not mapped by the process - in those cases a page fault will occur and the process will be killed.
static err_t verify_user_buffer(void *start, size_t length) {
    u64 start_addr = (u64)start;
    if (start_addr + length < start_addr)
        return ERR_INVALID_ADDRESS;
    if (start_addr + length > USER_ADDR_UPPER_BOUND)
        return ERR_INVALID_ADDRESS;
    return 0;
}

// Returns the length of the message
err_t syscall_message_get_length(size_t i, size_t *length) {
    err_t err;
    err = verify_user_buffer(length, sizeof(size_t));
    if (err)
        return err;
    Handle handle;
    err = handle_get(&cpu_local->current_process->process.handles, i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_WRONG_HANDLE_TYPE;
    *length = handle.message->data_size;
    return 0;
}

// Reads the message data into the provided userspace buffer
// The buffer must be large enough to fit the entire message.
err_t syscall_message_read(size_t i, void *data) {
    err_t err;
    Handle handle;
    err = handle_get(&cpu_local->current_process->process.handles, i, &handle);
    if (err)
        return err;
    if (handle.type != HANDLE_TYPE_MESSAGE)
        return ERR_WRONG_HANDLE_TYPE;
    err = verify_user_buffer(data, handle.message->data_size);
    if (err)
        return err;
    memcpy(data, handle.message->data, handle.message->data_size);
    return 0;
}
