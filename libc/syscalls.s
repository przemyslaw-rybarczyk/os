global map_pages
global print_char

; This file implements the C interface for system calls

map_pages:
  mov rax, 0
  syscall
  ret

print_char:
  mov rax, 1
  syscall
  ret
