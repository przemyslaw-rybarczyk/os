global tss
global tss_end
global percpu_init
global userspace_init
global sched_yield
global sched_start
global process_exit
global process_start

extern syscalls
extern malloc
extern sched_replace_process
extern sched_switch_process
extern process_free
extern load_elf_file
extern page_map_free_contents
extern page_free
extern free
extern stack_free
extern framebuffer_lock
extern framebuffer_unlock
extern print_string

struc PerCPU
  .current_process: resq 1
  .tss: resq 1
  .user_rsp: resq 1
endstruc

struc Process
  .rsp: resq 1
  .kernel_stack: resq 1
  .page_map: resq 1
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
MSR_SFMASK equ 0xC0000084
MSR_GS_BAS equ 0xC0000101

RFLAGS_IF equ 1 << 9
RFLAGS_DF equ 1 << 10

PAGE_SIZE equ 1 << 12

PAGE_PRESENT equ 1 << 0
PAGE_WRITE equ 1 << 1
PAGE_GLOBAL equ 1 << 8
PAGE_NX equ 1 << 63

SYSCALLS_NUM equ 3

ERR_INVALID_SYSCALL_NUMBER equ 1
ERR_NO_MEMORY equ 4

section .rodata

elf_load_fail_msg: db `Failed to load ELF file\n\0`

section .text

syscall_handler:
  ; Check if the syscall number is valid
  cmp rax, SYSCALLS_NUM
  jae .no_syscall
  ; SWAPGS to get access to per-CPU data through the GS segment
  swapgs
  ; Save the user stack pointer and load the kernel stack pointer
  ; The user stack pointer is temporarily saved in the CPU-local data and then pushed to the kernel stack once it's loaded.
  ; We keep interrupts disabled while we do this to avoid an interrupt occurring with no stack set up.
  mov [gs:PerCPU.user_rsp], rsp
  mov rsp, [gs:PerCPU.current_process]
  mov rsp, [rsp + Process.kernel_stack]
  push qword [gs:PerCPU.user_rsp]
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

; Allocate per-CPU data and set the GS base so it can be used
percpu_init:
  mov rdi, PerCPU_size
  call malloc
  test rax, rax
  jz .malloc_fail
  mov ecx, MSR_GS_BAS
  mov rdx, rax
  shr rdx, 32
  wrmsr
  xor rax, rax
  ret
.malloc_fail:
  mov rax, ERR_NO_MEMORY
  ret

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
  ; SFMASK needs to contain the mask that is applied to RFLAGS when a syscall occurs.
  ; The bits that are set in SFMASK are cleared in RFLAGS.
  ; We set the register to clear IF and DF.
  ; IF is cleared so that the kernel can load the kernel stack before re-enabling interrupts.
  ; The ABI requires DF to be cleared before calling the handler.
  mov ecx, MSR_SFMASK
  xor eax, ~(RFLAGS_DF | RFLAGS_IF)
  xor edx, edx
  wrmsr
  ret

; Start the scheduler
; This function is called at the end of kernel initialization.
sched_start:
  ; Get the first process to run
  call sched_replace_process
  ; Skip the part of sched_yield where current process state is saved, since there is no current process yet
  jmp sched_yield.from_no_process

; Ends the current process
process_exit:
  mov rbx, [gs:PerCPU.current_process]
  ; Free the userspace page map
  mov rdi, [rbx + Process.page_map]
  call page_map_free_contents
  ; From this point on, interrupts must be disabled.
  ; If a context switch occurred while the process is being freed, it wouldn't be possible to come back to it.
  cli
  ; Free the PML4
  mov rdi, [rbx + Process.page_map]
  call page_free
  ; Get the kernel stack address so it can be freed later
  ; We can't free it now, since we're still using it.
  mov r12, [rbx + Process.kernel_stack]
  ; Free the process control block
  mov rdi, rbx
  call free
  ; Get the next process to run
  call sched_replace_process
  mov r13, [gs:PerCPU.current_process]
  ; Restore the stack pointer
  mov rsp, [r13 + Process.rsp]
  ; Free the old kernel stack
  mov rdi, r12
  call stack_free
  ; Skip the part of sched_yield where current process state is saved and the new process's stack pointer is restored,
  ; since there is no process to save and we already restored the new stack pointer.
  mov rax, r13
  jmp sched_yield.from_removed_process

; Preempt the current process and run a different one
sched_yield:
  mov rax, [gs:PerCPU.current_process]
  ; Save process state on the stack
  ; Since this function will only be called from kernel code, we only need to save the non-scratch registers.
  ; The instruction pointer was already saved on the stack when this function was called.
  push rbx
  push rbp
  push r12
  push r13
  push r14
  push r15
  mov [rax + Process.rsp], rsp
  ; Set current_process to the next process to be run.
  call sched_switch_process
.from_no_process:
  mov rax, [gs:PerCPU.current_process]
  ; Restore the stack pointer
  mov rsp, [rax + Process.rsp]
.from_removed_process:
  ; Set the RSP0 in the TSS
  mov rdx, [gs:PerCPU.tss]
  mov rcx, [rax + Process.kernel_stack]
  mov [rdx + TSS.rsp0], rcx
  ; Restore process state
  pop r15
  pop r14
  pop r13
  pop r12
  pop rbp
  pop rbx
  ; Load process page map
  mov rdx, [rax + Process.page_map]
  mov cr3, rdx
  ; Return to the process
  ret

; Enter a process
; When a process is created, its instruction pointer is set to the start of this function,
; which finishes initializing process state and jumps to the actual entry point.
; Takes the following arugments from the stack (listed top to bottom):
; const u8 *file, size_t file_length, u64 arg
process_start:
  ; Load the process
  pop rdi
  pop rsi
  push rax
  mov rdx, rsp
  call load_elf_file
  test rax, rax
  jnz .fail
  ; Get process entry point
  pop rax
  ; Set the process argument
  pop rdi
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
  xor r8, r8
  xor r9, r9
  xor r10, r10
  xor r11, r11
  swapgs
  ; Jump to the process using an IRET
  iretq
.fail:
  sti
  call framebuffer_lock
  mov rdi, elf_load_fail_msg
  call print_string
  call framebuffer_unlock
.fail_loop:
  hlt
  jmp .fail_loop
