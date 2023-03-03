#pragma once

#include "types.h"

bool stack_init(void);
void *stack_alloc(void);
void stack_free(void *stack_bottom);
