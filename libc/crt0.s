global _start

extern main
extern _string_init
extern _alloc_init
extern _stdio_init

section .text

STACK_START equ 0x00007FFFFFF00000
STACK_LENGTH equ 0x100000

MAP_PAGES_WRITE equ 1

SYSCALL_MAP_PAGES equ 0
SYSCALL_PROCESS_EXIT equ 1

_start:
  ; Map the stack
  mov rax, SYSCALL_MAP_PAGES
  mov rdi, STACK_START
  mov rsi, STACK_LENGTH
  mov rdx, MAP_PAGES_WRITE
  syscall
  test rax, rax
  jnz .fail
  ; Set the stack pointer
  mov rsp, STACK_START + STACK_LENGTH
  ; Call initialization functions
  call _string_init
  call _alloc_init
  test rax, rax
  jnz .fail
  call _stdio_init
.start:
  ; Call main()
  call main
  ; Afet main() returns, exit from the process
.exit:
  mov rax, SYSCALL_PROCESS_EXIT
  syscall
.fail:
  jmp .exit
