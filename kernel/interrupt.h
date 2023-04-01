#pragma once

#include "error.h"

err_t interrupt_init(bool bsp);
void interrupt_disable(void);
void interrupt_enable(void);
