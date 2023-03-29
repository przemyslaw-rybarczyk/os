%include "kernel/percpu.inc"

global percpu_init

extern malloc

MSR_GS_BAS equ 0xC0000101

ERR_NO_MEMORY equ 4

; Allocate per-CPU data and set the GS base so it can be used
percpu_init:
  push rdi
  ; Allocate the per-CPU data structure
  mov rdi, PerCPU_size
  call malloc
  test rax, rax
  jz .malloc_fail
  pop rdi
  ; Set the idle stack
  mov [rax + PerCPU.idle_stack], rdi
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
