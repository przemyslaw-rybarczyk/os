#pragma once

#include "types.h"

typedef u32 spinlock_t;

#define SPINLOCK_FREE 0
#define SPINLOCK_USED 1

void preempt_disable(void);
void preempt_enable(void);
void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
