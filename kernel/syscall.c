#include "types.h"

#include "channel.h"
#include "error.h"
#include "framebuffer.h"
#include "interrupt.h"
#include "page.h"
#include "process.h"

#define MAP_PAGES_WRITE (1ull << 0)
#define MAP_PAGES_EXECUTE (1ull << 1)

err_t syscall_map_pages(u64 start, u64 length, u64 flags) {
    if (flags & ~(MAP_PAGES_WRITE | MAP_PAGES_EXECUTE))
        return ERR_INVALID_ARG;
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
    process_clear_handle(i);
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
};
