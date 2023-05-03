#pragma once

#include <types.h>
#include <error.h>

#define MAP_PAGES_WRITE (1ull << 0)
#define MAP_PAGES_EXECUTE (1ull << 1)

// Possible return values of system calls:
// - Every syscall taking a handle as an argument will return ERR_KERNEL_INVALID_HANDLE or ERR_KERNEL_WRONG_HANDLE_TYPE if the handle is invalid
//   - handle_free() is an exception, as it returns void
// - Every syscall taking a pointer as an argument will return ERR_KERNEL_INVALID_ADDRESS if the address is invalid
//   - map_pages() will return ERR_KERNEL_INVALID_ADDRESS if trying to map region that includes kernel memory
// - The following syscalls may return ERR_KERNEL_NO_MEMORY:
//     map_pages(), channel_call(), mqueue_receive(), message_reply(), channel_call_bounded()
//   - message_reply() with length 0 will not return ERR_KERNEL_NO_MEMORY
// - The following syscalls may return ERR_KERNEL_INVALID_ARG if one of their arguments has an invalid value:
//     map_pages(), message_reply_error(), message_read_bounded()
// - The following syscalls may return ERR_KERNEL_MESSAGE_TOO_SHORT or ERR_KERNEL_MESSAGE_TOO_LONG:
//     message_read_bounded(), reply_read_bounded(), channel_call_bounded()
// - map_pages() may return ERR_KERNEL_PAGE_ALREADY_MAPPED
// - channel_call() and channel_call_bounded() may return a user error code

err_t map_pages(u64 start, u64 length, u64 flags);
_Noreturn void process_exit(void);
void process_yield(void);
err_t message_get_length(handle_t i, size_t *length);
err_t message_read(handle_t i, void *data);
err_t channel_call(handle_t channel_i, size_t message_size, const void *message_data, handle_t *reply_i_ptr);
err_t mqueue_receive(handle_t mqueue_i, uintptr_t tag[2], handle_t *message_i_ptr);
err_t message_reply(handle_t message_i, size_t reply_size, const void *reply_data);
void handle_free(handle_t i);
err_t message_reply_error(handle_t message_i, err_t error);
err_t message_read_bounded(handle_t i, void *data, size_t *length, size_t min_length, size_t max_length, err_t err_low, err_t err_high);
err_t reply_read_bounded(handle_t i, void *data, size_t *length, size_t min_length, size_t max_length);
err_t channel_call_bounded(
    handle_t channel_i, size_t message_size, const void *message_data,
    void *reply_data, size_t *reply_length, size_t min_length, size_t max_length);

static inline err_t message_read_sized(handle_t i, void *data, size_t length, err_t error) {
    return message_read_bounded(i, data, NULL, length, length, error, error);
}

static inline err_t message_read_sized_2(handle_t i, void *data, size_t length, err_t err_low, err_t err_high) {
    return message_read_bounded(i, data, NULL, length, length, err_low, err_high);
}

static inline err_t reply_read_sized(handle_t i, void *data, size_t length) {
    return reply_read_bounded(i, data, NULL, length, length);
}

static inline err_t channel_call_sized(handle_t i, size_t message_size, const void *message_data, void *reply_data, size_t reply_length) {
    return channel_call_bounded(i, message_size, message_data, reply_data, NULL, reply_length, reply_length);
}
