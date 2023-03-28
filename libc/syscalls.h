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
err_t channel_send(size_t channel_i, size_t message_size, void *message_data);
err_t channel_receive(size_t channel_i, size_t *message_i);
