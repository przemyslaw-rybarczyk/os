global interrupt_handlers

extern general_exception_handler

IDT_ENTRIES_NUM equ 0x30

; Define a handler for each interrupt
; The handler saves the scratch registers and calls the actual handler function (written in C).
%assign i 0
%rep IDT_ENTRIES_NUM
interrupt_handler_%+i:
  ; Save all scratch registers - they may be overwritten by the C function
  push rax
  push rcx
  push rdx
  push rsi
  push rdi
  push r8
  push r9
  push r10
  push r11
  ; Call the handler function with two arguments: the number of the interrupt
  ; and the value the stack pointer had at the start of the interrupt handler.
  mov rdi, i
  lea rsi, [rsp - 9 * 8]
  call general_exception_handler
  ; Restore the scratch registers and return
  pop r11
  pop r10
  pop r9
  pop r8
  pop rdi
  pop rsi
  pop rdx
  pop rcx
  pop rax
  iretq
%assign i i+1
%endrep

; Put the address of each interrupt handler in a table
; These addresses will be placed into the IDT by the initialization code.
interrupt_handlers:
%assign i 0
%rep IDT_ENTRIES_NUM
  dq interrupt_handler_%+i
%assign i i+1
%endrep
