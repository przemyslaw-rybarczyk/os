#pragma once

#include <types.h>
#include <error.h>

#define MAP_PAGES_WRITE (1ull << 0)
#define MAP_PAGES_EXECUTE (1ull << 1)

// Possible return values of system calls:
// - Every syscall taking a handle as an argument will return ERR_INVALID_HANDLE or ERR_WRONG_HANDLE_TYPE if the handle is invalid
//   - handle_free() is an exception, as it returns void
// - Every syscall taking a pointer as an argument will return ERR_INVALID_ADDRESS if the address is invalid
//   - map_pages() will return ERR_INVALID_ADDRESS if trying to map region that includes kernel memory
// - The following syscalls may return ERR_NO_MEMORY:
//     map_pages(), channel_call(), mqueue_receive(), message_reply()
//   - message_reply() with length 0 will not return ERR_NO_MEMORY
// - The following syscalls may return ERR_INVALID_ARG if one of their arguments has an invalid value that:
//     map_pages(), message_reply_error()
// - map_pages() may also return ERR_PAGE_ALREADY_MAPPED

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
