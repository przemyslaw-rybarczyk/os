global map_pages
global process_exit
global process_yield
global message_get_length
global message_read
global channel_call
global mqueue_receive
global message_reply
global handle_free
global message_reply_error
global channel_call_read
global resource_get
global mqueue_create
global mqueue_add_channel
global mqueue_add_channel_resource
global channel_create
global channel_send
global time_get
global message_resource_read
global process_time_get
global process_wait
global channel_call_async

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
  mov r10, rcx
  syscall
  ret

channel_call:
  mov rax, 5
  syscall
  ret

mqueue_receive:
  mov rax, 6
  mov r10, rcx
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

channel_call_read:
  mov rax, 10
  mov r10, rcx
  syscall
  ret

resource_get:
  mov rax, 11
  syscall
  ret

mqueue_create:
  mov rax, 12
  syscall
  ret

mqueue_add_channel:
  mov rax, 13
  mov r10, rcx
  syscall
  ret

mqueue_add_channel_resource:
  mov rax, 14
  mov r10, rcx
  syscall
  ret

channel_create:
  mov rax, 15
  syscall
  ret

channel_send:
  mov rax, 16
  syscall
  ret

time_get:
  mov rax, 17
  syscall
  ret

message_resource_read:
  mov rax, 18
  mov r10, rcx
  syscall
  ret

process_time_get:
  mov rax, 19
  syscall
  ret

process_wait:
  mov rax, 20
  syscall
  ret

channel_call_async:
  mov rax, 21
  mov r10, rcx
  syscall
  ret
