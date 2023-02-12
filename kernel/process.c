#include "types.h"
#include "process.h"

#include "alloc.h"
#include "page.h"
#include "segment.h"
#include "string.h"

extern void jump_to_current_process(void);

typedef struct RegisterState {
    u64 rax;
    u64 rcx;
    u64 rdx;
    u64 rbx;
    u64 rbp;
    u64 rsp;
    u64 rsi;
    u64 rdi;
    u64 r8;
    u64 r9;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
    u64 rip;
    u64 rflags;
    u64 cs;
    u64 ss;
} RegisterState;

typedef struct Process {
    RegisterState registers;
    u64 kernel_stack_phys;
} Process;

Process *current_process;

bool spawn_process(u64 entry) {
    Process *process = malloc(sizeof(Process));
    memset(process, 0, sizeof(Process));
    process->registers.rip = entry;
    process->registers.cs = SEGMENT_USER_CODE | SEGMENT_RING_3;
    process->registers.ss = SEGMENT_USER_DATA | SEGMENT_RING_3;
    process->kernel_stack_phys = page_alloc();
    if (process->kernel_stack_phys == 0)
        return false;
    current_process = process;
    jump_to_current_process();
}
