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
global channel_call_bounded
global resource_get
global mqueue_create
global mqueue_add_channel
global mqueue_add_channel_resource
global channel_create

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
  mov rax, 10
  mov r10, rcx
  syscall
  ret

channel_call_bounded:
  mov rax, 11
  mov r10, rcx
  syscall
  ret

resource_get:
  mov rax, 12
  syscall
  ret

mqueue_create:
  mov rax, 13
  syscall
  ret

mqueue_add_channel:
  mov rax, 14
  mov r10, rcx
  syscall
  ret

mqueue_add_channel_resource:
  mov rax, 15
  mov r10, rcx
  syscall
  ret

channel_create:
  mov rax, 16
  syscall
  ret
