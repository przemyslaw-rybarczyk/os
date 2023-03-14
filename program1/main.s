global _start

_start:
.loop:
  mov rax, 1
  syscall
  jmp .loop
