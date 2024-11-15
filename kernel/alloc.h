#pragma once

#include "types.h"
#include "error.h"

err_t _alloc_init(void);
void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);
