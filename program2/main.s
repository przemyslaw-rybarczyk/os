global _start

_start:
  mov rax, '-'
.loop:
  syscall
  xchg rdi, rax
  syscall
  xchg rdi, rax
  jmp .loop
