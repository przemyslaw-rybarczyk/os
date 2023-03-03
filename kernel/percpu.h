#pragma once

#include "types.h"

typedef struct ProcessQueueNode ProcessQueueNode;
typedef struct TSS TSS;

typedef struct PerCPU {
    ProcessQueueNode *current_process;
    TSS *tss;
} PerCPU;

#define cpu_local ((__seg_gs PerCPU *)0)

bool percpu_init(void);
