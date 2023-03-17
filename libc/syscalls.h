#pragma once

#include <types.h>

#define MAP_PAGES_WRITE (1ull << 0)
#define MAP_PAGES_EXECUTE (1ull << 1)

bool map_pages(u64 start, u64 length, u64 flags);
void print_char(u64 c);
_Noreturn void process_exit(void);
void process_yield(void);
