global tss
global tss_end
global tss_init

TSS_DESCRIPTOR equ 0x28

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
