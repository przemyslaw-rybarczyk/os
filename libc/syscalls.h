#pragma once

#include <types.h>
#include <error.h>

#define MAP_PAGES_WRITE (1ull << 0)
#define MAP_PAGES_EXECUTE (1ull << 1)

err_t map_pages(u64 start, u64 length, u64 flags);
_Noreturn void process_exit(void);
void process_yield(void);
err_t message_get_length(handle_t i, size_t *length);
err_t message_read(handle_t i, void *data);
err_t channel_call(handle_t channel_i, size_t message_size, void *message_data, handle_t *reply_i_ptr);
err_t mqueue_receive(handle_t mqueue_i, uintptr_t tag[2], handle_t *message_i_ptr);
err_t message_reply(handle_t message_i, size_t reply_size, void *reply_data);
void handle_free(handle_t i);
err_t message_reply_error(handle_t message_i, err_t error);
