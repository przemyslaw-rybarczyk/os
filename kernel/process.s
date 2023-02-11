global tss
global tss_end
global userspace_init
global jump_to_program

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
  dq 0
  times 90 db 0 ; various variable we don't use
  dw tss_end - tss ; I/O Map Base Address - set to the size of the TSS to make it empty
tss_end:

section .text

extern print_char

syscall_handler:
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
  call print_char
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

jump_to_program:
  ; Set RSP0 in TSS
  mov [tss.rsp0], rsp
  ; Jump to the process by setting up the stack and executing an IRET
  mov rax, rsp
  push SEGMENT_USER_DATA | SEGMENT_RING_3 ; SS
  push rax ; RSP
  pushf ; RFLAGS
  push SEGMENT_USER_CODE | SEGMENT_RING_3 ; CS
  push rdi ; RIP
  iretq
