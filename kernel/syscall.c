#include "types.h"

#include "channel.h"
#include "error.h"
#include "framebuffer.h"
#include "interrupt.h"
#include "page.h"
#include "percpu.h"
#include "process.h"

#define MAP_PAGES_WRITE (UINT64_C(1) << 0)
#define MAP_PAGES_EXECUTE (UINT64_C(1) << 1)

err_t syscall_map_pages(u64 start, u64 length, u64 flags) {
    if (flags & ~(MAP_PAGES_WRITE | MAP_PAGES_EXECUTE))
        return ERR_KERNEL_INVALID_ARG;
    return map_user_pages(start, length, flags & MAP_PAGES_WRITE, flags & MAP_PAGES_EXECUTE);
}

err_t syscall_process_exit(void) {
    process_exit();
}

err_t syscall_process_yield(void) {
    process_switch();
    return 0;
}

err_t syscall_handle_free(handle_t i) {
    handle_clear(&cpu_local->current_process->handles, i);
    return 0;
}

const void * const syscalls[] = {
    syscall_map_pages,
    syscall_process_exit,
    syscall_process_yield,
    syscall_message_get_length,
    syscall_message_read,
    syscall_channel_call,
    syscall_mqueue_receive,
    syscall_message_reply,
    syscall_handle_free,
    syscall_message_reply_error,
    syscall_message_read_bounded,
    syscall_channel_call_bounded,
    syscall_resource_get,
    syscall_mqueue_create,
    syscall_mqueue_add_channel,
    syscall_mqueue_add_channel_resource,
    syscall_channel_create,
    syscall_channel_send,
};
