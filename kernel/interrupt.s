global interrupt_handlers

extern general_exception_handler
extern pit_irq_handler
extern keyboard_irq_handler
extern mouse_irq_handler

IDT_ENTRIES_NUM equ 0x30
IDT_EXCEPTIONS_NUM equ 0x20

IDT_PIT_IRQ equ 0x20
IDT_KEYBOARD_IRQ equ 0x21
IDT_MOUSE_IRQ equ 0x2C

%define interrupt_has_handler(i) ((i) < IDT_EXCEPTIONS_NUM || (i) == IDT_PIT_IRQ || (i) == IDT_KEYBOARD_IRQ || (i) == IDT_MOUSE_IRQ)

; Define a wrapper handler for each interrupt that has a handler function
; The handler saves the scratch registers and calls the actual handler function (written in C).
%assign i 0
%rep IDT_ENTRIES_NUM
%if interrupt_has_handler(i)
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
%if i < IDT_EXCEPTIONS_NUM
  ; For exceptions we call the handler function with two arguments: the number of the interrupt
  ; and the value the stack pointer had at the start of the interrupt handler.
  mov rdi, i
  lea rsi, [rsp - 9 * 8]
  call general_exception_handler
%elif i == IDT_PIT_IRQ
  call pit_irq_handler
%elif i == IDT_KEYBOARD_IRQ
  call keyboard_irq_handler
%elif i == IDT_MOUSE_IRQ
  call mouse_irq_handler
%endif
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
%endif
%assign i i+1
%endrep

; Put the address of each interrupt handler in a table
; These addresses will be placed into the IDT by the initialization code.
; Interrupts without handlers are represented as 0.
interrupt_handlers:
%assign i 0
%rep IDT_ENTRIES_NUM
%if interrupt_has_handler(i)
  dq interrupt_handler_%+i
%else
  dq 0
%endif
%assign i i+1
%endrep