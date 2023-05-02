global map_pages
global print_char
global process_exit
global process_yield
global message_get_length
global message_read
global channel_call
global mqueue_receive
global message_reply
global handle_free
global message_reply_error
global message_read_bounded

; This file implements the C interface for system calls

map_pages:
  mov rax, 0
  syscall
  ret

process_exit:
  mov rax, 1
  syscall

process_yield:
  mov rax, 2
  syscall
  ret

message_get_length:
  mov rax, 3
  syscall
  ret

message_read:
  mov rax, 4
  syscall
  ret

channel_call:
  mov rax, 5
  mov r10, rcx
  syscall
  ret

mqueue_receive:
  mov rax, 6
  syscall
  ret

message_reply:
  mov rax, 7
  syscall
  ret

handle_free:
  mov rax, 8
  syscall
  ret

message_reply_error:
  mov rax, 9
  syscall
  ret

message_read_bounded:
  xchg bx, bx
  push rbx
  mov rbx, [rsp + 2 * 8]
  mov rax, 10
  mov r10, rcx
  syscall
  pop rbx
  ret
