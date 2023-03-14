global _start

_start:
  mov rdx, '-'
.loop:
  mov rax, 1
  syscall
  xchg rdi, rdx
  mov rax, 1
  syscall
  xchg rdi, rdx
  jmp .loop
