section .rodata

hello: db `Hello, world!\0`

section .text

_start:
  mov rax, hello
.loop:
  mov dil, [rax]
  and rdi, 0xFF
  test rdi, rdi
  jz .end
  syscall
  add rax, 1
  jmp .loop
.end:
  jmp .end
