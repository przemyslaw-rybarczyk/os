global tss
global tss_end
global userspace_init
global sched_yield
global sched_start

extern current_process
extern schedule_next_process
extern print_char_locked

struc Process
  .rax: resq 1
  .rdx: resq 1
  .rcx: resq 1
  .rbx: resq 1
  .rbp: resq 1
  .rsp: resq 1
  .rsi: resq 1
  .rdi: resq 1
  .r8: resq 1
  .r9: resq 1
  .r10: resq 1
  .r11: resq 1
  .r12: resq 1
  .r13: resq 1
  .r14: resq 1
  .r15: resq 1
  .rip: resq 1
  .rflags: resq 1
  .cs: resq 1
  .ss: resq 1
  .kernel_stack_phys: resq 1
endstruc

SEGMENT_KERNEL_CODE equ 0x08
SEGMENT_KERNEL_DATA equ 0x10
SEGMENT_USER_DATA equ 0x18
SEGMENT_USER_CODE equ 0x20
TSS_DESCRIPTOR equ 0x28

SEGMENT_RING_3 equ 0x03

MSR_STAR equ 0xC0000081
MSR_LSTAR equ 0xC0000082
MSR_SFMASK equ 0xC0000084

RFLAGS_IF equ 1 << 9

section .rodata

; Task State Segment
tss:
  dd 0 ; unused
.rsp0:
  ; RSP0 - the only part of the TSS we actually use
  ; Holds the ring 0 stack pointer so it can be restored when an interrupt occurs.
  dq KERNEL_STACK_BOTTOM
  times 90 db 0 ; various variable we don't use
  dw tss_end - tss ; I/O Map Base Address - set to the size of the TSS to make it empty
tss_end:

section .text

PAGE_SIZE equ 1 << 12

PAGE_PRESENT equ 1 << 0
PAGE_WRITE equ 1 << 1
PAGE_GLOBAL equ 1 << 8
PAGE_NX equ 1 << 63

KERNEL_STACK_PML4E equ 0x1FE
RECURSIVE_PML4E equ 0x100

KERNEL_STACK_BOTTOM equ (0xFFFF << 48) | ((KERNEL_STACK_PML4E + 1) << 39)
KERNEL_STACK_TOP equ KERNEL_STACK_BOTTOM - PAGE_SIZE
KERNEL_STACK_PTE equ (0xFFFF << 48) | (RECURSIVE_PML4E << 39) | (KERNEL_STACK_PML4E << 30) | 0x3FFFFFF8

KERNEL_SWAP_STACK_BOTTOM equ KERNEL_STACK_BOTTOM - 2 * PAGE_SIZE
KERNEL_SWAP_STACK_TOP equ KERNEL_SWAP_STACK_BOTTOM - PAGE_SIZE
KERNEL_SWAP_STACK_PTE equ KERNEL_STACK_PTE - 2 * 8

syscall_handler:
  ; Set up the kernel stack
  ; We keep interrupts disabled while we do that to avoid an interrupt occurring with no stack set up.
  mov rsp, KERNEL_STACK_BOTTOM
  sti
  ; Save all scratch registers
  push rax
  push rcx
  push rdx
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  ; Call print_char with rdi as argument
  and rdi, 0xFF
  call print_char_locked
  ; Restore scratch registers and return
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rdx
  pop rcx
  pop rax
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
  ; SFMASK needs to contain the mask that is applied to RFLAGS when a syscall occurs.
  ; The bits that are set in SFMASK are cleared in RFLAGS.
  ; We set the register to clear IF so that the kernel can load the kernel stack before re-enabling interrupts.
  mov ecx, MSR_SFMASK
  xor eax, RFLAGS_IF
  xor edx, edx
  wrmsr
  ret

; Start the scheduler
; This function is called at the end of kernel initialization.
sched_start:
  ; Skip the part of sched_yield where current process state is saved,
  ; since there is no current process yet.
  jmp sched_yield.start_next_process

sched_yield:
  push rax
  mov rax, [current_process]
  ; Save process state
  ; RIP and RSP are set so that when the process is returned to, it will be as if it returned from this function.
  pop qword [rax + Process.rax]
  pop qword [rax + Process.rip]
  mov [rax + Process.rcx], rcx
  mov [rax + Process.rdx], rdx
  mov [rax + Process.rbx], rbx
  mov [rax + Process.rbp], rbp
  mov [rax + Process.rsp], rsp
  mov [rax + Process.rsi], rsi
  mov [rax + Process.rdi], rdi
  mov [rax + Process.r8], r8
  mov [rax + Process.r9], r9
  mov [rax + Process.r10], r10
  mov [rax + Process.r11], r11
  mov [rax + Process.r12], r12
  mov [rax + Process.r13], r13
  mov [rax + Process.r14], r14
  mov [rax + Process.r15], r15
  pushfq
  pop qword [rax + Process.rflags]
  mov [rax + Process.cs], cs
  mov [rax + Process.ss], ss
  ; Set current_process to the next process to be run.
  call schedule_next_process
.start_next_process:
  mov rax, [current_process]
  ; Load the process kernel stack
  ; Since the process expects its kernel stack to be in the same location as the currently loaded kernel stack,
  ; we move the current kernel stack to a different location (the swap stack) before mapping the process kernel stack in its place.
  mov rbx, KERNEL_STACK_PTE
  mov rcx, KERNEL_SWAP_STACK_PTE
  mov r8, KERNEL_STACK_TOP
  mov r9, KERNEL_SWAP_STACK_TOP
  mov rdx, [rbx]
  mov [rcx], rdx
  invlpg [r9]
  add rsp, KERNEL_SWAP_STACK_BOTTOM - KERNEL_STACK_BOTTOM
  mov rdx, [rax + Process.kernel_stack_phys]
  mov rsi, PAGE_NX | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT
  or rdx, rsi
  mov [rbx], rdx
  invlpg [r8]
  ; Set up the stack for an IRET
  push qword [rax + Process.ss]
  push qword [rax + Process.rsp]
  push qword [rax + Process.rflags]
  push qword [rax + Process.cs]
  push qword [rax + Process.rip]
  ; Restore process registers
  mov rcx, [rax + Process.rcx]
  mov rdx, [rax + Process.rdx]
  mov rbx, [rax + Process.rbx]
  mov rbp, [rax + Process.rbp]
  mov rsi, [rax + Process.rsi]
  mov rdi, [rax + Process.rdi]
  mov r8, [rax + Process.r8]
  mov r9, [rax + Process.r9]
  mov r10, [rax + Process.r10]
  mov r11, [rax + Process.r11]
  mov r12, [rax + Process.r12]
  mov r13, [rax + Process.r13]
  mov r14, [rax + Process.r14]
  mov r15, [rax + Process.r15]
  mov rax, [rax + Process.rax]
  ; Jump to the process using an IRET
  iretq
