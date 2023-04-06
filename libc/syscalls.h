#pragma once

#include <types.h>
#include <error.h>

#define MAP_PAGES_WRITE (1ull << 0)
#define MAP_PAGES_EXECUTE (1ull << 1)

err_t map_pages(u64 start, u64 length, u64 flags);
void print_char(u64 c);
_Noreturn void process_exit(void);
void process_yield(void);
err_t message_get_length(size_t i, size_t *length);
err_t message_read(size_t i, void *data);
err_t channel_call(size_t channel_i, size_t message_size, void *message_data, size_t *reply_i_ptr);
err_t channel_receive(size_t channel_i, size_t *message_i);
err_t message_reply(size_t message_i, size_t reply_size, void *reply_data);
void handle_free(size_t i);
err_t message_reply_error(size_t message_i, err_t error);
