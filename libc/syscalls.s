global map_pages
global print_char
global process_exit
global process_yield

; This file implements the C interface for system calls

map_pages:
  mov rax, 0
  syscall
  ret

print_char:
  mov rax, 1
  syscall
  ret

process_exit:
  mov rax, 2
  syscall

process_yield:
  mov rax, 3
  syscall
  ret
