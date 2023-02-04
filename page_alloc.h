#pragma once

#include "types.h"

void page_alloc_init(void);
u64 page_alloc(void);
void page_free(u64 page);
u64 get_free_memory_size(void);
