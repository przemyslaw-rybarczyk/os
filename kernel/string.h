#pragma once

#include "types.h"

void memset(void *dest, int c, size_t n);
void memcpy(void * restrict dest, const void * restrict src, size_t n);
void memmove(void *dest, const void *src, size_t n);
