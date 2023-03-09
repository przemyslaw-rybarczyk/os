global _start

_start:
.loop:
  syscall
  jmp .loop
