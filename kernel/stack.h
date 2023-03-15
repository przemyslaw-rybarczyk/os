#pragma once

#include "types.h"
#include "error.h"

err_t stack_init(void);
void *stack_alloc(void);
void stack_free(void *stack_bottom);
