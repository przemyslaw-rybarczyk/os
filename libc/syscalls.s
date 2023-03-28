global map_pages
global print_char
global process_exit
global process_yield
global message_get_length
global message_read
global channel_send
global channel_receive

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

message_get_length:
  mov rax, 4
  syscall
  ret

message_read:
  mov rax, 5
  syscall
  ret

channel_send:
  mov rax, 6
  syscall
  ret

channel_receive:
  mov rax, 7
  syscall
  ret
