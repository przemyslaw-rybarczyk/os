#include "types.h"
#include "process.h"

#include "alloc.h"
#include "channel.h"
#include "framebuffer.h"
#include "handle.h"
#include "included_programs.h"
#include "interrupt.h"
#include "input.h"
#include "page.h"
#include "percpu.h"
#include "resource.h"
#include "segment.h"
#include "smp.h"
#include "spinlock.h"
#include "stack.h"
#include "string.h"

#define RFLAGS_IF (UINT64_C(1) << 9)

typedef struct FXSAVEArea {
    u16 fcw;
    u16 fsw;
    u8 ftw;
    u8 reserved1;
    u16 fop;
    u64 fip;
    u64 fdp;
    u32 mxcsr;
    u32 mxcsr_mask;
    u64 mm[2][8];
    u64 xmm[2][16];
    u64 reserved[12];
} __attribute__((packed)) FXSAVEArea;

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
// The process is not placed in the queue and its stack is not initialized.
err_t process_create(Process **process_ptr, ResourceList resources) {
    err_t err;
    // Allocate a process control block
    Process *process = malloc(sizeof(Process));
    if (process == NULL) {
        err = ERR_KERNEL_NO_MEMORY;
        goto fail_process_alloc;
    }
    // Allocate the FXSAVE area and initialize it with default values
    process->fxsave_area = malloc(sizeof(FXSAVEArea));
    if (process->fxsave_area == NULL) {
        err = ERR_KERNEL_NO_MEMORY;
        goto fail_fxsave_area_alloc;
    }
    memset(process->fxsave_area, 0, sizeof(FXSAVEArea));
    process->fxsave_area->fcw = 0x037F;
    process->fxsave_area->mxcsr = 0x00001F80u;
    // Allocate a process page map
    u64 page_map = page_alloc_clear();
    if (page_map == 0) {
        err = ERR_KERNEL_NO_MEMORY;
        goto fail_page_map_alloc;
    }
    process->page_map = page_map;
    // Copy the kernel mappings
    memcpy((u64 *)PHYS_ADDR(page_map) + 0x100, (u64 *)PHYS_ADDR(get_pml4()) + 0x100, 0x100 * 8);
    // Allocate a kernel stack
    process->kernel_stack = stack_alloc();
    if (process->kernel_stack == NULL) {
        err = ERR_KERNEL_NO_MEMORY;
        goto fail_stack_alloc;
    }
    // Initialize the handle list
    err = handle_list_init(&process->handles);
    if (err)
        goto fail_handle_list_init;
    // Intialize remaining fields
    process->running_time = 0;
    process->resources = resources;
    *process_ptr = process;
    return 0;
fail_handle_list_init:
    stack_free(process->kernel_stack);
fail_stack_alloc:
    page_free(process->page_map);
fail_page_map_alloc:
    free(process->fxsave_area);
fail_fxsave_area_alloc:
    free(process);
fail_process_alloc:
    return err;
}

// Set up the stack for a user process running a given executable file
// The message passed, if not NULL, will be freed after the process is loaded.
void process_set_user_stack(Process *process, const u8 *file, size_t file_length, Message *message) {
    u64 *rsp = process->kernel_stack;
    // Arguments to process_start()
    *--rsp = (u64)message;
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
}

// Set up the stack for a kernel thread with a given entry point
void process_set_kernel_stack(Process *process, void *entry_point) {
    u64 *rsp = process->kernel_stack;
    // Used by process_switch() - same as in process_set_user_stack(), but with a different entry point
    *--rsp = (u64)entry_point;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 1;
    process->rsp = rsp;
}

// Add a process to the queue of running processes
void process_enqueue(Process *process) {
    spinlock_acquire(&scheduler_lock);
    // Add the process to end of the queue
    process_queue_add(&scheduler_queue, process);
    // Wake up an idle core if there is one
    if (idle_core_list != NULL) {
        send_wakeup_ipi(idle_core_list->lapic_id);
        idle_core_list = idle_core_list->next_cpu;
    }
    spinlock_release(&scheduler_lock);
}

// Set up the initial processes
err_t process_setup(void) {
    err_t err;
    framebuffer_mqueue = mqueue_alloc();
    if (framebuffer_mqueue == NULL)
        return ERR_KERNEL_NO_MEMORY;
    process_spawn_mqueue = mqueue_alloc();
    if (process_spawn_mqueue == NULL)
        return ERR_KERNEL_NO_MEMORY;
    framebuffer_data_channel = channel_alloc();
    if (framebuffer_data_channel == NULL)
        return ERR_KERNEL_NO_MEMORY;
    framebuffer_size_channel = channel_alloc();
    if (framebuffer_size_channel == NULL)
        return ERR_KERNEL_NO_MEMORY;
    keyboard_key_channel = channel_alloc();
    if (keyboard_key_channel == NULL)
        return ERR_KERNEL_NO_MEMORY;
    mouse_button_channel = channel_alloc();
    if (mouse_button_channel == NULL)
        return ERR_KERNEL_NO_MEMORY;
    mouse_move_channel = channel_alloc();
    if (mouse_move_channel == NULL)
        return ERR_KERNEL_NO_MEMORY;
    mouse_scroll_channel = channel_alloc();
    if (mouse_scroll_channel == NULL)
        return ERR_KERNEL_NO_MEMORY;
    process_spawn_channel = channel_alloc();
    if (process_spawn_channel == NULL)
        return ERR_KERNEL_NO_MEMORY;
    channel_set_mqueue(framebuffer_data_channel, framebuffer_mqueue, (MessageTag){FB_MQ_TAG_DATA, 0});
    channel_set_mqueue(framebuffer_size_channel, framebuffer_mqueue, (MessageTag){FB_MQ_TAG_SIZE, 0});
    channel_set_mqueue(process_spawn_channel, process_spawn_mqueue, (MessageTag){0, 0});
    Process *framebuffer_kernel_thread;
    err = process_create(&framebuffer_kernel_thread, (ResourceList){0, NULL});
    if (err)
        return err;
    err = process_create(&process_spawn_kernel_thread, (ResourceList){0, NULL});
    if (err)
        return err;
    process_set_kernel_stack(framebuffer_kernel_thread, framebuffer_kernel_thread_main);
    process_set_kernel_stack(process_spawn_kernel_thread, process_spawn_kernel_thread_main);
    channel_add_ref(framebuffer_size_channel);
    channel_add_ref(framebuffer_data_channel);
    channel_add_ref(keyboard_key_channel);
    channel_add_ref(mouse_button_channel);
    channel_add_ref(mouse_move_channel);
    channel_add_ref(mouse_scroll_channel);
    channel_add_ref(process_spawn_channel);
    Process *init_process;
    ResourceListEntry *init_resources = malloc(7 * sizeof(ResourceListEntry));
    if (init_resources == NULL)
        return ERR_KERNEL_NO_MEMORY;
    init_resources[0] = (ResourceListEntry){
        resource_name("video/size"), {
            RESOURCE_TYPE_CHANNEL_SEND,
            {.channel = framebuffer_size_channel}}};
    init_resources[1] = (ResourceListEntry){
        resource_name("video/data"), {
            RESOURCE_TYPE_CHANNEL_SEND,
            {.channel = framebuffer_data_channel}}};
    init_resources[2] = (ResourceListEntry){
        resource_name("keyboard/key"), {
            RESOURCE_TYPE_CHANNEL_RECEIVE,
            {.channel = keyboard_key_channel}}};
    init_resources[3] = (ResourceListEntry){
        resource_name("mouse/button"), {
            RESOURCE_TYPE_CHANNEL_RECEIVE,
            {.channel = mouse_button_channel}}};
    init_resources[4] = (ResourceListEntry){
        resource_name("mouse/move"), {
            RESOURCE_TYPE_CHANNEL_RECEIVE,
            {.channel = mouse_move_channel}}};
    init_resources[5] = (ResourceListEntry){
        resource_name("mouse/scroll"), {
            RESOURCE_TYPE_CHANNEL_RECEIVE,
            {.channel = mouse_scroll_channel}}};
    init_resources[6] = (ResourceListEntry){
        resource_name("process/spawn"), {
            RESOURCE_TYPE_CHANNEL_SEND,
            {.channel = process_spawn_channel}}};
    err = process_create(&init_process, (ResourceList){7, init_resources});
    if (err)
        return err;
    process_set_user_stack(init_process, included_file_window, included_file_window_end - included_file_window, NULL);
    process_enqueue(framebuffer_kernel_thread);
    process_enqueue(process_spawn_kernel_thread);
    process_enqueue(init_process);
    return 0;
}

// Free the current process
// Does not free any information that is necessary to switch to the process when it's running in kernel mode,
// as it needs to be freed separately and with interrupts disabled.
void process_free_contents(void) {
    page_map_free_contents(cpu_local->current_process->page_map);
    handle_list_free(&cpu_local->current_process->handles);
    resource_list_free(&cpu_local->current_process->resources);
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

Process *process_spawn_kernel_thread;
Channel *process_spawn_channel;
MessageQueue *process_spawn_mqueue;

// Expected format for process spawn message:
// Data:
//   size_t resource_message_count
//   ResourceName message_resource_names[resource_message_count]
//   ResourceName handle_resource_names[handle_count]
//   {
//       size_t message_length
//       u8 message[message_length]
//   }[resource_message_count]
//   u8 elf_file[]
// Handles:
//   <handle> resource_handle_count[handle_count]

_Noreturn void process_spawn_kernel_thread_main(void) {
    err_t err;
    while (1) {
        Message *message;
        // Get message from user process
        mqueue_receive(process_spawn_mqueue, &message, false);
        size_t message_offset = 0;
        if (message->data_size < sizeof(size_t)) {
            err = ERR_INVALID_ARG;
            goto fail;
        }
        size_t resource_message_count = *(size_t *)(message->data + message_offset);
        message_offset += sizeof(size_t);
        size_t resources_size = resource_message_count + message->handles_size;
        if (message->data_size < message_offset + resources_size * sizeof(ResourceName)) {
            err = ERR_INVALID_ARG;
            goto fail;
        }
        // Create resource list
        ResourceListEntry *resources = malloc(resources_size * sizeof(ResourceListEntry));
        if (resources_size != 0 && resources == NULL) {
            err = ERR_NO_MEMORY;
            goto fail;
        }
        ResourceName *resource_names = (ResourceName *)(message->data + message_offset);
        message_offset += resources_size * sizeof(ResourceName);
        for (size_t i = 0; i < resource_message_count; i++) {
            if (message->data_size < message_offset + sizeof(size_t)) {
                err = ERR_INVALID_ARG;
                goto fail_resource_message;
            }
            size_t resource_message_length = *(size_t *)(message->data + message_offset);
            message_offset += sizeof(size_t);
            if (message->data_size < message_offset + resource_message_length) {
                err = ERR_INVALID_ARG;
                goto fail_resource_message;
            }
            Message *resource_message = message_alloc_copy(resource_message_length, message->data + message_offset);
            if (resource_message == NULL) {
                err = ERR_NO_MEMORY;
                goto fail_resource_message;
            }
            message_offset += resource_message_length;
            resources[i].name = resource_names[i];
            resources[i].resource = (Resource){RESOURCE_TYPE_MESSAGE, .message = resource_message};
            continue;
fail_resource_message:
            for (size_t j = 0; j < i; j++)
                message_free(resources[j].resource.message);
            goto fail;
        }
        for (size_t i = 0; i < message->handles_size; i++) {
            resources[resource_message_count + i].name = resource_names[resource_message_count + i];
            switch (message->handles[i].type) {
            case ATTACHED_HANDLE_TYPE_CHANNEL_SEND:
                channel_add_ref(message->handles[i].channel);
                resources[resource_message_count + i].resource = (Resource){RESOURCE_TYPE_CHANNEL_SEND, .channel = message->handles[i].channel};
                break;
            case ATTACHED_HANDLE_TYPE_CHANNEL_RECEIVE:
                channel_add_ref(message->handles[i].channel);
                resources[resource_message_count + i].resource = (Resource){RESOURCE_TYPE_CHANNEL_RECEIVE, .channel = message->handles[i].channel};
                break;
            }
        }
        // Create the process
        Process *process;
        err = process_create(&process, (ResourceList){resources_size, resources});
        if (err) {
            resource_list_free(&(ResourceList){resources_size, resources});
            err = user_error_code(err);
            goto fail;
        }
        // Set up the process stack to load provided ELF file and free the message upon starting
        process_set_user_stack(process, message->data + message_offset, message->data_size - message_offset, message);
        process_enqueue(process);
        continue;
fail:
        message_reply_error(message, err);
        message_free(message);
    }
}
