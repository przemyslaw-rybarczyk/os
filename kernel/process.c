#include "types.h"
#include "process.h"

#include "alloc.h"
#include "channel.h"
#include "elf.h"
#include "handle.h"
#include "included_programs.h"
#include "interrupt.h"
#include "page.h"
#include "percpu.h"
#include "segment.h"
#include "smp.h"
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

static ProcessQueue scheduler_queue;
static PerCPU *idle_core_list;

// Add a process to the end of a queue
void process_queue_add(ProcessQueue *queue, Process *process) {
    process->next_process = NULL;
    if (queue->start == NULL) {
        queue->start = process;
        queue->end = process;
    } else {
        queue->end->next_process = process;
        queue->end = process;
    }
}

// Remove a process from the start of a queue and return it
// If the queue is empty, returns NULL.
Process *process_queue_remove(ProcessQueue *queue) {
    if (queue->start == NULL)
        return NULL;
    Process *process = queue->start;
    queue->start = queue->start->next_process;
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
    // Used by process_switch() - return address, saved registers and interrupt disable count
    // We set the return address to the entry point of process_start, zero all registers, and set interrupts as disabled once.
    *--rsp = (u64)process_start;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 1;
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
    // Add the process to end of the queue
    process_queue_add(&scheduler_queue, process);
    // Wake up an idle core if there is one
    if (idle_core_list != NULL) {
        send_wakeup_ipi(idle_core_list->lapic_id);
        idle_core_list = idle_core_list->next_cpu;
    }
    spinlock_release(&scheduler_lock);
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
    err = handle_set(&server_process->handles, 1, (Handle){HANDLE_TYPE_CHANNEL_OUT, {.channel = channel}});
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
        err = handle_set(&client_process->handles, 1, (Handle){HANDLE_TYPE_CHANNEL_IN, {.channel = channel}});
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
// Must be called with interrupts disabled.
void sched_replace_process(void) {
    spinlock_acquire(&scheduler_lock);
    // Get a process from the queue
    // If the queue is empty, wait until it isn't
    while ((cpu_local->current_process = process_queue_remove(&scheduler_queue)) == NULL) {
        // If there are no processes in the queue, add the CPU to the idle CPU list
        cpu_local->next_cpu = idle_core_list;
        idle_core_list = cpu_local->self;
        spinlock_release(&scheduler_lock);
        // The idle flag is set and will only be cleared by a wakeup IPI.
        cpu_local->idle = true;
        // Preemption is disabled since interrupts are enabled while waiting but there is no valid process.
        preempt_disable();
        // Wait for a wakeup IPI to occur
        // The HLT instruction has to immediately follow an STI to avoid a race condition where an interrupt occurs before HLT.
        // The effect of STI is always delayed by at least one instruction, so the interrupt can't occur before the HLT.
        while (cpu_local->idle)
            asm volatile ("sti; hlt; cli");
        preempt_enable();
        spinlock_acquire(&scheduler_lock);
    }
    spinlock_release(&scheduler_lock);
}

// Return the current process to the end of the queue and set `cpu_local->current_process` to the next process in the queue
// The current scheduler is a basic round-robin scheduler.
void sched_switch_process(void) {
    spinlock_acquire(&scheduler_lock);
    // Get the next process from the queue
    Process *next_process = process_queue_remove(&scheduler_queue);
    // If there are no other processes to run, return to the current process
    if (next_process == NULL) {
        spinlock_release(&scheduler_lock);
        return;
    }
    // Add the current process to the queue and replace it with the new process
    process_queue_add(&scheduler_queue, cpu_local->current_process);
    cpu_local->current_process = next_process;
    spinlock_release(&scheduler_lock);
}

err_t process_get_handle(handle_t i, Handle *handle) {
    return handle_get(&cpu_local->current_process->handles, i, handle);
}

err_t process_add_handle(Handle handle, handle_t *i_ptr) {
    return handle_add(&cpu_local->current_process->handles, handle, i_ptr);
}

void process_clear_handle(handle_t i) {
    return handle_clear(&cpu_local->current_process->handles, i);
}
