global _start

extern main

section .rodata

stack_alloc_error_msg: db `Error: failed to allocate stack\n\0`

section .text

STACK_START equ 0x00007FFFFFF00000
STACK_LENGTH equ 0x100000

MAP_PAGES_WRITE equ 1

SYSCALL_MAP_PAGES equ 0
SYSCALL_PRINT_CHAR equ 1

_start:
  mov rbx, rdi
  ; Map the stack
  mov rax, SYSCALL_MAP_PAGES
  mov rdi, STACK_START
  mov rsi, STACK_LENGTH
  mov rdx, MAP_PAGES_WRITE
  syscall
  test rax, 1
  jz .fail
  ; Set the stack pointer
  mov rsp, STACK_START + STACK_LENGTH
  mov rdi, rbx
  call main
  ; Afet main() returns, loop forever
.halt:
  jmp .halt
.fail:
  ; Print the error message
  mov rdx, stack_alloc_error_msg
.print_loop:
  mov rdi, [rdx]
  test rdi, rdi
  jz .halt
  mov rax, SYSCALL_PRINT_CHAR
  syscall
  add rdx, 1
  jmp .print_loop
