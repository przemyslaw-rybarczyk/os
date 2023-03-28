#pragma once

#include "types.h"

typedef struct Process Process;
typedef struct TSS TSS;

typedef struct PerCPU {
    Process *current_process;
    TSS *tss;
    u64 user_rsp;
} PerCPU;

#define cpu_local ((__seg_gs PerCPU *)0)

err_t percpu_init(void);
