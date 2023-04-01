%include "kernel/percpu.inc"

global percpu_init

extern malloc
extern memset

MSR_GS_BAS equ 0xC0000101

ERR_NO_MEMORY equ 4

; Allocate per-CPU data and set the GS base so it can be used
; Takes the idle stack base as an argument.
percpu_init:
  push rdi
  ; Allocate the per-CPU data structure
  mov rdi, PerCPU_size
  call malloc
  test rax, rax
  jz .malloc_fail
  push rax
  ; Clear the allocated structure
  mov rdi, rax
  xor rsi, rsi
  mov rdx, PerCPU_size
  call memset
  pop rax
  pop rdi
  ; Set the idle stack
  mov [rax + PerCPU.idle_stack], rdi
  ; Initialize interrupts as disabled once
  mov qword [rax + PerCPU.interrupt_disable], 1
  ; Set the GS base
  mov ecx, MSR_GS_BAS
  mov rdx, rax
  shr rdx, 32
  wrmsr
  xor rax, rax
  ret
.malloc_fail:
  mov rax, ERR_NO_MEMORY
  ret
