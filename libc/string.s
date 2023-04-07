global _string_init
global memset
global memcpy
global memmove

CPUID_FLAGS equ 7
CPUID_FLAGS_EBX_ERMS equ 1 << 9

section .bss

erms_supported: resb 1

section .text

; Initialize the memory manipulation functions
_string_init:
  push rbx
  ; Check for ERMS support through CPUID and set the erms_supported flag accordingly
  mov eax, CPUID_FLAGS
  mov ecx, 0
  cpuid
  test ebx, CPUID_FLAGS_EBX_ERMS
  setnz [erms_supported]
  pop rbx
  ret

; rdi - void *dest
; esi - int c
; rdx - size_t n
memset:
  mov r8, rdi
  mov eax, esi
  mov rcx, rdx
  ; If ERMS is supported, just fill memory using a REP STOSB
  cmp byte [erms_supported], 0
  jnz .has_erms
  ; Otherwise, fill using quadwords and then fill the remaining bytes
  ; The value of RAX has to be adjusted to contain the value being filled with in its every byte
  mov r9, 0x0101010101010101
  imul rax, r9
  shr rcx, 3
  rep stosq
  mov rcx, rdx
  and rcx, 7
.has_erms:
  rep stosb
  mov rax, r8
  ret

; rdi - void * restrict dest
; rsi - const void * restrict src
; rdx - size_t n
memcpy:
  mov rax, rdi
  mov rcx, rdx
  ; If ERMS is supported, just copy everything using a REP MOVSB
  cmp byte [erms_supported], 0
  jnz .has_erms
  ; Otherwise, copy using quadwords and then copy the remaining bytes
  shr rcx, 3
  rep movsq
  mov rcx, rdx
  and rcx, 7
.has_erms:
  rep movsb
  ret

; rdi - void * dest
; rsi - const void * src
; rdx - size_t n
memmove:
  ; If the destination buffer comes after the source buffer, we can just do a forwards copy, which our memcpy already implements
  cmp rdi, rsi
  jbe memcpy
  ; If there is no overlap between the buffers, memcpy() is enough
  lea r8, [rsi + rdx]
  cmp r8, rdi
  jbe memcpy
  ; Otherwise, we need to copy the data backwards
  ; We set the direction flag for this purpose.
  mov rax, rdi
  mov rcx, rdx
  std
  ; Check for ERMS
  cmp byte [erms_supported], 0
  jnz .has_erms
  ; If there is no ERMS, first copy using quadwords and then copy the remaining bytes
  sub rdx, 8
  add rdi, rdx
  add rsi, rdx
  shr rcx, 3
  rep movsq
  add rdi, 7
  add rsi, 7
  mov rcx, rdx
  and rcx, 7
  rep movsb
  cld
  ret
.has_erms:
  ; If there is ERMS, just copy everything with a REP MOVSB
  sub rdx, 1
  add rdi, rdx
  add rsi, rdx
  rep movsb
  cld
  ret
