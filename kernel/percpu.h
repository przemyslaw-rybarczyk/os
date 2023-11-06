#pragma once

#include "types.h"
#include "error.h"

typedef struct Process Process;
typedef struct TSS TSS;

// Per-CPU data
// Each CPU has one instance of this structure, which is accesses through the GS segment.
typedef struct PerCPU {
    // Holds the structure's address
    // Used to get a pointer to the per-CPU data structure without needing to read an MSR.
    struct PerCPU *self;
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
    // TSC value at start of currently running timeslice
    u64 timeslice_start;
    // The ID of the CPU's LAPIC
    // Used for sending IPIs.
    u32 lapic_id;
    // Indicates whether there is a pending delayed preemption
    // Set by the timer interrupt handler if a preemption should occur, but preemption is blocked.
    // Will be performed at the next available opportunity.
    bool preempt_delayed;
    // Is set if the CPU is currently idle and waiting for a process to execute.
    // Cleared by the wakeup IPI handler.
    bool idle;
    // Used to form the list of idle CPU cores
    struct PerCPU *next_cpu;
} PerCPU;

#define cpu_local ((__seg_gs PerCPU *)0)

err_t percpu_init(void *stack);
