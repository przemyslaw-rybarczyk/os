%include "kernel/percpu.inc"

global interrupt_handlers
global interrupt_disable
global interrupt_enable

extern general_exception_handler
extern pit_irq_handler
extern keyboard_irq_handler
extern mouse_irq_handler
extern wakeup_ipi_handler
extern halt_ipi_handler

IDT_ENTRIES_NUM equ 0x30
IDT_EXCEPTIONS_NUM equ 0x20

IDT_PIT_IRQ equ 0x20
IDT_KEYBOARD_IRQ equ 0x21
IDT_MOUSE_IRQ equ 0x22
IDT_WAKEUP_IPI equ 0x2D
IDT_HALT_IPI equ 0x2E
IDT_SPURIOUS_INT equ 0x2F

%define interrupt_has_handler(i) ((i) < IDT_EXCEPTIONS_NUM || (i) == IDT_PIT_IRQ || (i) == IDT_KEYBOARD_IRQ || (i) == IDT_MOUSE_IRQ || (i) == IDT_WAKEUP_IPI || (i) == IDT_HALT_IPI || (i) == IDT_SPURIOUS_INT)
%define interrupt_pushes_error_code(i) ((i) == 0x08 || (i) == 0x0A || (i) == 0x0B || (i) == 0x0C || (i) == 0x0D || (i) == 0x0E || (i) == 0x11 || (i) == 0x15 || (i) == 0x1D || (i) == 0x1E)

; Define a wrapper handler for each interrupt that has a handler function
; The handler saves the scratch registers and calls the actual handler function (written in C).
%assign i 0
%rep IDT_ENTRIES_NUM
%if interrupt_has_handler(i)
interrupt_handler_%+i:
  ; Perform a SWAPGS if the interrupt occurred while in userspace
  ; This is tested by checking the lower two bits of the CS register pushed to the stack.
%if interrupt_pushes_error_code(i)
  test qword [rsp + 16], 3
%else
  test qword [rsp + 8], 3
%endif
  jz .int_from_kernel
  swapgs
.int_from_kernel:
  ; Clear direction flag, as required by the ABI
  cld
  ; Record that interrupts have been disabled
  ; This is necessary to prevent delayed preemptions.
  add qword gs:[PerCPU.interrupt_disable], 1
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
  ; For exceptions we call the handler function with three arguments: the number of the interrupt,
  ; the value the stack pointer had at the start of the interrupt handler, and the error code if the interrupt pushes one.
%if interrupt_pushes_error_code(i)
  mov rdx, [rsp + 9 * 8]
  mov rdi, i
  lea rsi, [rsp + 10 * 8]
%else
  mov rdi, i
  lea rsi, [rsp + 9 * 8]
%endif
  call general_exception_handler
%elif i == IDT_PIT_IRQ
  call pit_irq_handler
%elif i == IDT_KEYBOARD_IRQ
  call keyboard_irq_handler
%elif i == IDT_MOUSE_IRQ
  call mouse_irq_handler
%elif i == IDT_WAKEUP_IPI
  call wakeup_ipi_handler
%elif i == IDT_HALT_IPI
  call halt_ipi_handler
%endif
  ; Record that interrupts will be re-enabled now
  sub qword gs:[PerCPU.interrupt_disable], 1
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
  ; Remove error code from stack if there is one
%if interrupt_pushes_error_code(i)
  add rsp, 8
%endif
  ; Perform SWAPGS again if returning to userspace
  test qword [rsp + 8], 3
  jz .ret_to_kernel
  swapgs
.ret_to_kernel:
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

; Disable interrupts
; Unlike the CLI instruction, this function allows nesting.
interrupt_disable:
  ; If interrupts are currently enabled, execute the CLI instruction
  cmp qword gs:[PerCPU.interrupt_disable], 0
  jne .no_disable
  cli
.no_disable:
  ; Increment the interrupt disable count
  add qword gs:[PerCPU.interrupt_disable], 1
  ret

; Enable interrupts
interrupt_enable:
  ; If the interrupt disable count is 1, decrement it and execute the STI instruction
  cmp qword gs:[PerCPU.interrupt_disable], 1
  jne .no_enable
  sub qword gs:[PerCPU.interrupt_disable], 1
  sti
  ret
  ; Otherwise, just decrement the count
.no_enable:
  sub qword gs:[PerCPU.interrupt_disable], 1
  ret
