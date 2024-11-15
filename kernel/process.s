%include "kernel/percpu.inc"

global timeslice_length
global tss
global tss_end
global userspace_init
global process_switch
global sched_start
global process_block
global process_exit
global process_start
global process_time_get
global cpu_haltable_num
global cpu_halted

extern interrupt_disable
extern interrupt_enable
extern syscalls
extern spinlock_release
extern sched_replace_process
extern sched_switch_process
extern process_free
extern load_elf_file
extern process_free_contents
extern page_free
extern free
extern stack_free
extern idle_page_map
extern message_alloc_copy
extern message_reply
extern message_reply_error
extern message_free
extern time_from_tsc
extern schedule_timeslice_interrupt
extern cancel_timeslice_interrupt
extern panic

struc Process
  .rsp: resq 1
  .kernel_stack: resq 1
  .page_map: resq 1
  .fxsave_area: resq 1
  .running_time: resq 1
endstruc

SEGMENT_KERNEL_CODE equ 0x08
SEGMENT_KERNEL_DATA equ 0x10
SEGMENT_USER_DATA equ 0x18
SEGMENT_USER_CODE equ 0x20
TSS_DESCRIPTOR equ 0x28

SEGMENT_RING_3 equ 0x03

TSS.rsp0 equ 4

MSR_STAR equ 0xC0000081
MSR_LSTAR equ 0xC0000082
MSR_FMASK equ 0xC0000084

RFLAGS_IF equ 1 << 9
RFLAGS_DF equ 1 << 10

PAGE_SIZE equ 1 << 12

PAGE_PRESENT equ 1 << 0
PAGE_WRITE equ 1 << 1
PAGE_GLOBAL equ 1 << 8
PAGE_NX equ 1 << 63

SYSCALLS_NUM equ 22

ERR_INVALID_SYSCALL_NUMBER equ 0xFFFFFFFFFFFF0001

section .bss

; Length of timeslice in TSC ticks
; Set by time_init().
timeslice_length: resq 1

; Number of CPUs that have completed the intialization process and can receive halt IPIs
cpu_haltable_num: resq 1

; Set if CPU was halted due to an error
cpu_halted: resb 1

section .rodata

process_block_panic_msg: db `process_block() called with more than one lock held\0`
process_exit_panic_msg: db `process_exit() called with locks held\0`
process_switch_panic_msg: db `process_switch() called with locks held\0`

section .text

; System call handler
; The system call number is passed in the RAX register. The arguments are passed in the RDI, RSI, RDX, R10, R8, R9 registers.
; The return value is given in RAX. All other arguments are left unchanged on return.
syscall_handler:
  ; Check if the syscall number is valid
  cmp rax, SYSCALLS_NUM
  jae .no_syscall
  ; SWAPGS to get access to per-CPU data through the GS segment
  swapgs
  ; Save the user stack pointer and load the kernel stack pointer
  ; The user stack pointer is temporarily saved in the CPU-local data and then pushed to the kernel stack once it's loaded.
  ; We keep interrupts disabled while we do this to avoid an interrupt occurring with no stack set up.
  mov gs:[PerCPU.user_rsp], rsp
  mov rsp, gs:[PerCPU.current_process]
  mov rsp, [rsp + Process.kernel_stack]
  push qword gs:[PerCPU.user_rsp]
  sti
  ; Save all scratch registers except RAX
  push rcx
  push rdx
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  ; Perform the system call
  mov rcx, r10
  mov r10, [syscalls + rax * 8]
  call r10
  ; Restore scratch registers and return
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rdx
  pop rcx
  ; Disable interrupts to avoid an interrupt occurring on the user stack
  cli
  ; Restore GS base
  swapgs
  ; Restore the user stack pointer
  pop rsp
  o64 sysret
.no_syscall:
  mov rax, ERR_INVALID_SYSCALL_NUMBER
  o64 sysret

userspace_init:
  ; Load the TSS
  mov ax, TSS_DESCRIPTOR
  ltr ax
  ; Set MSRs for syscalls
  ; There are three MSRs that need to be set to use the SYSCALL/SYSRET instructions.
  ; STAR needs to contain in its highest and second highest word the bases for setting user and kernel selectors.
  ; On a SYSCALL instruction, CS is set to (kernel base) and SS to (kernel base) + 8.
  ; On a SYSRET instruction, CS is set to (user base) + 16 and SS to (user base) + 8.
  mov ecx, MSR_STAR
  xor eax, eax
  mov edx, ((SEGMENT_USER_DATA - 8) << 16) | SEGMENT_KERNEL_CODE
  wrmsr
  ; LSTAR needs to contain the address of the syscall handler.
  mov ecx, MSR_LSTAR
  mov rax, syscall_handler
  mov rdx, rax
  shr rdx, 32
  wrmsr
  ; FMASK needs to contain the mask that is applied to RFLAGS when a syscall occurs.
  ; The bits that are set in FMASK are cleared in RFLAGS.
  ; We set the register to clear IF and DF.
  ; IF is cleared so that the kernel can load the kernel stack before re-enabling interrupts.
  ; The ABI requires DF to be cleared before calling the handler.
  mov ecx, MSR_FMASK
  mov eax, RFLAGS_DF | RFLAGS_IF
  xor edx, edx
  wrmsr
  ret

; Called at the start of every timeslice
timeslice_start:
  ; Get TSC value
  rdtsc
  mov edi, eax
  shl rdx, 32
  or rdi, rdx
  ; Set start of timeslice
  mov gs:[PerCPU.timeslice_start], rdi
  ; Set interrupt timer to go off after timeslice ends
  add rdi, [timeslice_length]
  call schedule_timeslice_interrupt
  ret

; Called at the end of every timeslice
timeslice_end:
  ; Disable interrupt timer
  call cancel_timeslice_interrupt
  ; Get TSC value
  rdtsc
  mov edi, eax
  shl rdx, 32
  or rdi, rdx
  ; Add time spent in timeslice to process time
  sub rdi, gs:[PerCPU.timeslice_start]
  mov rax, gs:[PerCPU.current_process]
  add [rax + Process.running_time], rdi
  ret

; Return the time spent running the current process up to this point
process_time_get:
  ; Get TSC value
  rdtsc
  mov edi, eax
  shl rdx, 32
  or rdi, rdx
  ; Subtract start of timeslice
  sub rdi, gs:[PerCPU.timeslice_start]
  ; Add running time up to start of current timeslice
  mov rax, gs:[PerCPU.current_process]
  add rdi, [rax + Process.running_time]
  call time_from_tsc
  ret

; Start the scheduler
; This function is called at the end of kernel initialization.
sched_start:
  ; Get the first process to run
  call sched_replace_process
  ; If another CPU has already halted due to an error, halt
  cmp byte [cpu_halted], 0
  jne .halt
  ; Indicate that this CPU can be halted via an IPI
  lock add qword [cpu_haltable_num], 1
  ; Skip the part of process_switch where current process state is saved, since there is no current process yet
  jmp process_switch.from_no_process
.halt:
  hlt
  jmp .halt

; Preempt the current process without returning it to the queue
; Takes a spinlock as argument. After saving process state, the spinlock is released.
; This is provided so that a process can safely place itself in a process queue protected by a lock before calling this function.
; Must be called with no locks held other than the one passed as argument.
; The argument may be NULL, in which case no lock is released.
process_block:
  cmp qword gs:[PerCPU.preempt_disable], 1
  jna .one_lock_held
  mov rdi, process_block_panic_msg
  call panic
.one_lock_held:
  push rdi
  ; Disable interrupts
  call interrupt_disable
  pop rdi
  mov rax, gs:[PerCPU.current_process]
  ; Save process state
  push rbx
  push rbp
  push r12
  push r13
  push r14
  push r15
  push qword gs:[PerCPU.interrupt_disable]
  mov rcx, [rax + Process.fxsave_area]
  o64 fxsave [rcx]
  mov [rax + Process.rsp], rsp
  ; Switch to the idle stack and page map
  mov rsp, gs:[PerCPU.idle_stack]
  mov rdx, idle_page_map
  mov cr3, rdx
  ; Release the spinlock
  test rdi, rdi
  jz .no_spinlock
  call spinlock_release
.no_spinlock:
  ; End the timeslice
  call timeslice_end
  ; Get the next process to run and jump to the appropriate part of process_switch
  call sched_replace_process
  jmp process_switch.from_no_process

; Ends the current process
; Must be called with no locks held.
process_exit:
  cmp qword gs:[PerCPU.preempt_disable], 0
  je .no_locks_held
  mov rdi, process_exit_panic_msg
  call panic
.no_locks_held:
  mov rbx, gs:[PerCPU.current_process]
  ; Free the process contents
  ; This does not free any parts of the process control block that are necessary to switch back to the process running in kernel mode.
  mov rdi, rbx
  call process_free_contents
  ; From this point on, interrupts must be disabled.
  ; If a context switch occurred while the process is being freed, it wouldn't be possible to come back to it.
  call interrupt_disable
  ; End the timeslice
  call timeslice_end
  ; Switch to the idle stack and page map
  mov rsp, gs:[PerCPU.idle_stack]
  mov rdx, idle_page_map
  mov cr3, rdx
  ; Free the PML4
  mov rdi, [rbx + Process.page_map]
  call page_free
  ; Free the process kernel stack
  mov rdi, [rbx + Process.kernel_stack]
  call stack_free
  ; Free the process control block
  mov rdi, rbx
  call free
  ; Get the next process to run
  call sched_replace_process
  ; Skip the part of process_switch where current process state is saved, since there is no process now
  jmp process_switch.from_no_process

; Preempt the current process and run a different one
; Must be called with no locks held.
process_switch:
  cmp qword gs:[PerCPU.preempt_disable], 0
  je .no_locks_held
  mov rdi, process_switch_panic_msg
  call panic
.no_locks_held:
  ; Disable interrupts
  call interrupt_disable
  ; End the timeslice
  call timeslice_end
  mov rax, gs:[PerCPU.current_process]
  ; Save process state on the stack
  ; Since this function will only be called from kernel code, we only need to save the non-scratch registers.
  ; The instruction pointer was already saved on the stack when this function was called.
  push rbx
  push rbp
  push r12
  push r13
  push r14
  push r15
  push qword gs:[PerCPU.interrupt_disable]
  mov rcx, [rax + Process.fxsave_area]
  o64 fxsave [rcx]
  mov [rax + Process.rsp], rsp
  ; Switch to the idle stack
  mov rsp, gs:[PerCPU.idle_stack]
  ; Set current_process to the next process to be run.
  call sched_switch_process
.from_no_process:
  mov rax, gs:[PerCPU.current_process]
  ; Restore the stack pointer
  mov rsp, [rax + Process.rsp]
  ; Set the RSP0 in the TSS
  mov rdx, gs:[PerCPU.tss]
  mov rcx, [rax + Process.kernel_stack]
  mov [rdx + TSS.rsp0], rcx
  ; Restore process state
  mov rcx, [rax + Process.fxsave_area]
  o64 fxrstor [rcx]
  pop qword gs:[PerCPU.interrupt_disable]
  pop r15
  pop r14
  pop r13
  pop r12
  pop rbp
  pop rbx
  ; Load process page map
  mov rdx, [rax + Process.page_map]
  mov cr3, rdx
  ; Start timeslice
  call timeslice_start
  ; Re-enable interrupts
  call interrupt_enable
  ; Return to the process
  ret

ERR_NO_MEMORY equ 0x3
ERR_KERNEL_NO_MEMORY equ 0xFFFFFFFFFFFF0003

; Enter a process
; When a process is created, its instruction pointer is set to the start of this function,
; which finishes initializing process state and jumps to the actual entry point.
; Takes the following arugments from the stack (listed top to bottom):
; const u8 *file, size_t file_length, Message *message
process_start:
  ; Load the process
  pop rdi
  pop rsi
  push rax
  mov rdx, rsp
  call load_elf_file
  test rax, rax
  jnz .fail
  ; Send empty reply to message and free it
  cmp qword [rsp + 8], 0
  jz .no_message
  xor rdi, rdi
  xor rsi, rsi
  call message_alloc_copy
  test rax, rax
  jz .fail
  mov rsi, rax
  mov rdi, [rsp + 8]
  call message_reply
  cmp rax, ERR_KERNEL_NO_MEMORY
  mov rax, ERR_NO_MEMORY
  je .fail
  mov rdi, [rsp + 8]
  call message_free
.no_message:
  ; Get process entry point
  pop rax
  add rsp, 8
  ; Set up the kernel stack for an IRET
  push SEGMENT_USER_DATA | SEGMENT_RING_3
  push 0 ; initialize RSP to 0 - it is the responsibility of user code to allocate a stack
  push RFLAGS_IF ; keep interrupts enabled
  push SEGMENT_USER_CODE | SEGMENT_RING_3
  push rax ; set RIP to the process entry point
  ; Initialize all registers to zero
  ; We only need to clear the scratch registers, as the other registers were set to zero when the thread was created.
  xor rax, rax
  xor rcx, rcx
  xor rdx, rdx
  xor rsi, rsi
  xor rdi, rdi
  xor r8, r8
  xor r9, r9
  xor r10, r10
  xor r11, r11
  swapgs
  ; Jump to the process using an IRET
  iretq
.fail:
  ; Send empty reply to message and free it
  mov rdi, [rsp + 8]
  test rdi, rdi
  jz .fail_no_message
  mov rsi, rax
  call message_reply_error
  mov rdi, [rsp + 8]
  call message_free
.fail_no_message:
  call process_exit
