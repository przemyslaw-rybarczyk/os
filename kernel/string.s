global memset
global memcpy
global memmove

; rdi - void *dest
; rsi - int c
; rdx - size_t n
memset:
    mov rax, rsi
    mov rcx, rdx
    rep stosb
    ret

; rdi - void * restrict dest
; rsi - const void * restrict src
; rdx - size_t n
memcpy:
    mov rcx, rdx
    rep movsb
    ret

; rdi - void * dest
; rsi - const void * src
; rdx - size_t n
memmove:
    mov rcx, rdx
    cmp rdi, rsi
    jae .forwards
.backwards:
    rep movsb
    ret
.forwards:
    std
    sub rdx, 1
    add rdi, rdx
    add rsi, rdx
    rep movsb
    cld
    ret
