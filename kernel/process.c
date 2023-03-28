#include "types.h"
#include "process.h"

#include "alloc.h"
#include "channel.h"
#include "elf.h"
#include "handle.h"
#include "included_programs.h"
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
    struct Process *next_process;
} Process;

extern void jump_to_current_process(void);
extern u8 process_start[];

static spinlock_t scheduler_lock;
static semaphore_t sched_queue_semaphore;

static Process *process_queue_start = NULL;
static Process *process_queue_end = NULL;

static void add_process_to_queue(Process *process) {
    if (process_queue_start == NULL) {
        process_queue_start = process;
        process_queue_end = process;
    } else {
        process_queue_end->next_process = process;
        process_queue_end = process;
    }
    process->next_process = NULL;
}

static Process *get_process_from_queue(void) {
    Process *process = process_queue_start;
    process_queue_start = process_queue_start->next_process;
    return process;
}

// Create a new process
// The process is not placed in the queue.
err_t process_create(const u8 *file, size_t file_length, Process **process_ptr) {
    err_t err;
    Process *process = malloc(sizeof(Process));
    if (process == NULL) {
        err = ERR_NO_MEMORY;
        goto fail_process_alloc;
    }
    // Allocate a process page map
    u64 page_map = page_alloc_clear();
    if (page_map == 0) {
        err = ERR_NO_MEMORY;
        goto fail_page_map_alloc;
    }
    process->page_map = page_map;
    // Copy the kernel mappings
    memcpy((u64 *)PHYS_ADDR(page_map) + 0x100, (u64 *)PHYS_ADDR(get_pml4()) + 0x100, 0x100 * 8);
    // Allocate a kernel stack
    process->kernel_stack = stack_alloc();
    if (process->kernel_stack == NULL) {
        err = ERR_NO_MEMORY;
        goto fail_stack_alloc;
    }
    // Initialize the handle list
    err = handle_list_init(&process->handles);
    if (err)
        goto fail_handle_list_init;
    // Initialize the kernel stack contents
    u64 *rsp = process->kernel_stack;
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
    process->rsp = rsp;
    *process_ptr = process;
    return 0;
fail_handle_list_init:
    stack_free(process->kernel_stack);
fail_stack_alloc:
    page_free(process->page_map);
fail_page_map_alloc:
    free(process);
fail_process_alloc:
    return err;
}

// Add a process to the queue of running processes
err_t process_enqueue(Process *process) {
    spinlock_acquire(&scheduler_lock);
    add_process_to_queue(process);
    spinlock_release(&scheduler_lock);
    semaphore_increment(&sched_queue_semaphore);
    return 0;
}

// Initialize processes
// Creates a number of client processes communitcating with a server process through a channel.
err_t process_setup(void) {
    err_t err;
    Channel *channel = channel_alloc();
    if (channel == NULL)
        return ERR_NO_MEMORY;
    Process *server_process;
    err = process_create(included_file_program2, included_file_program2_end - included_file_program2, &server_process);
    if (err)
        return err;
    err = handle_set(&server_process->handles, 1, (Handle){HANDLE_TYPE_CHANNEL, {.channel = channel}});
    if (err)
        return err;
    for (u64 i = 0; i < 8; i++) {
        Process *client_process;
        err = process_create(included_file_program1, included_file_program1_end - included_file_program1, &client_process);
        if (err)
            return err;
        u8 *message_data = malloc(sizeof(u64));
        if (message_data == NULL)
            return ERR_NO_MEMORY;
        u64 arg = 'A' + i;
        memcpy(message_data, &arg, sizeof(u64));
        Message *message = message_alloc(sizeof(u64), message_data);
        if (message == NULL)
            return ERR_NO_MEMORY;
        err = handle_set(&client_process->handles, 0, (Handle){HANDLE_TYPE_MESSAGE, {.message = message}});
        if (err)
            return err;
        channel_add_ref(channel);
        err = handle_set(&client_process->handles, 1, (Handle){HANDLE_TYPE_CHANNEL, {.channel = channel}});
        if (err)
            return err;
        err = process_enqueue(client_process);
        if (err)
            return err;
    }
    err = process_enqueue(server_process);
    if (err)
        return err;
    return 0;
}

// Free the current process
// Does not free any information that is necessary to switch to the process when it's running in kernel mode,
// as it needs to be freed separately and with interrupts disabled.
void process_free_contents(void) {
    page_map_free_contents(cpu_local->current_process->page_map);
    handle_list_free(&cpu_local->current_process->handles);
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

err_t process_get_handle(size_t i, Handle *handle) {
    return handle_get(&cpu_local->current_process->handles, i, handle);
}

err_t process_add_handle(Handle handle, size_t *i_ptr) {
    return handle_add(&cpu_local->current_process->handles, handle, i_ptr);
}
