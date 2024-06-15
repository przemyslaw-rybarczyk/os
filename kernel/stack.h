#pragma once

#include "types.h"
#include "error.h"

#include "page.h"

#define STACK_PML4E UINT64_C(0x1FE)
#define KERNEL_INIT_STACK ASSEMBLE_ADDR_PML4E(STACK_PML4E, 0)

err_t stack_init(void);
void *stack_alloc(void);
void stack_free(void *stack_bottom);
