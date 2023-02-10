global tss
global tss_end
global tss_init
global jump_to_program

SEGMENT_KERNEL_CODE equ 0x08
SEGMENT_KERNEL_DATA equ 0x10
SEGMENT_USER_CODE equ 0x18
SEGMENT_USER_DATA equ 0x20
TSS_DESCRIPTOR equ 0x28

SEGMENT_RING_3 equ 0x03

; Task State Segment
tss:
  dd 0 ; unused
.rsp0:
  ; RSP0 - the only part of the TSS we actually use
  ; Holds the ring 0 stack pointer so it can be restored when an interrupt occurs.
  dq 0
  times 90 db 0 ; various variable we don't use
  dw tss_end - tss ; I/O Map Base Address - set to the size of the TSS to make it empty
tss_end:

tss_init:
  ; Load the TSS
  mov ax, TSS_DESCRIPTOR
  ltr ax
  ret

jump_to_program:
  ; Set RSP0 in TSS
  mov [tss.rsp0], rsp
  ; Jump to the process by setting up the stack and executing an IRET
  push SEGMENT_USER_DATA | SEGMENT_RING_3 ; SS
  push 0 ; RSP
  pushf ; RFLAGS
  push SEGMENT_USER_CODE | SEGMENT_RING_3 ; CS
  push rdi ; RIP
  iretq
