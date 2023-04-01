#pragma once

#include "types.h"
#include "error.h"

typedef struct Process Process;
typedef struct TSS TSS;

// Per-CPU data
// Each CPU has one instance of this structure, which is accesses through the GS segment.
typedef struct PerCPU {
    // Currently running process
    Process *current_process;
    // Task State Segment
    TSS *tss;
    // Used to temporarily hold the user stack pointer when context switching into a syscall handler
    u64 user_rsp;
    // The stack used when no process is running
    void *idle_stack;
    // Number of times interrupts have been disabled
    u64 interrupt_disable;
    // Number of times preemption has been disabled
    u64 preempt_disable;
    // Indicates whether there is a pending delayed preemption
    // Set by the timer interrupt handler if a preemption should occur, but preemption is blocked.
    // Will be performed at the next available opportunity.
    bool preempt_delayed;
} PerCPU;

#define cpu_local ((__seg_gs PerCPU *)0)

err_t percpu_init(void *stack);
