%include "kernel/percpu.inc"

global percpu_init

extern malloc
extern memset

MSR_GS_BAS equ 0xC0000101

ERR_NO_MEMORY equ 0xFFFFFFFFFFFF0002

; Initialize per-CPU data and set the GS base so it can be used
; Takes the preallocated PerCPU strucutre and idle stack base as an arguments.
percpu_init:
  push rdi
  push rsi
  ; Clear the PerCPU structure
  xor rsi, rsi
  mov rdx, PerCPU_size
  call memset
  pop rsi
  pop rdi
  ; Set the self pointer
  mov [rdi + PerCPU.self], rdi
  ; Set the idle stack
  mov [rdi + PerCPU.idle_stack], rsi
  ; Initialize interrupts as disabled once
  mov qword [rdi + PerCPU.interrupt_disable], 1
  ; Set the GS base
  mov ecx, MSR_GS_BAS
  mov rax, rdi
  mov rdx, rdi
  shr rdx, 32
  wrmsr
  ret
