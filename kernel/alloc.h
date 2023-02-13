#pragma once

#include "types.h"

bool alloc_init(void);
void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);
void print_debug_heap_info(void);