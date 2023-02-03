global pic_init

PIC1_COMMAND equ 0x20
PIC1_DATA equ 0x21
PIC2_COMMAND equ 0xA0
PIC2_DATA equ 0xA1

ICW1_ICW4 equ 1 << 0
ICW1_INIT equ 1 << 4
ICW4_8086 equ 1 << 0

PIC1_KEYBOARD_LINE equ 1
PIC1_SLAVE_LINE equ 2
PIC2_MOUSE_LINE equ 4

; Vector offsets are set so that IRQ interrupt numbers immediately follow those of the exceptions.
PIC1_VECTOR_OFFSET equ 0x20
PIC2_VECTOR_OFFSET equ 0x28

pic_init:
  ; Perform the initialization sequence for both PICs
  ; Send ICW1
  mov al, ICW1_INIT | ICW1_ICW4
  out PIC1_COMMAND, al
  out PIC2_COMMAND, al
  ; Send ICW2
  mov al, PIC1_VECTOR_OFFSET
  out PIC1_DATA, al
  mov al, PIC2_VECTOR_OFFSET
  out PIC2_DATA, al
  ; Send ICW3
  mov al, 1 << PIC1_SLAVE_LINE
  out PIC1_DATA, al
  mov al, PIC1_SLAVE_LINE
  out PIC2_DATA, al
  ; Send ICW4
  mov al, ICW4_8086
  out PIC1_DATA, al
  out PIC2_DATA, al
  ; Set the masks
  ; We enable IRQs from the keyboard and the mouse.
  mov al, ~(1 << PIC1_KEYBOARD_LINE | 1 << PIC1_SLAVE_LINE)
  out PIC1_DATA, al
  mov al, ~(1 << PIC2_MOUSE_LINE)
  out PIC2_DATA, al
  ret
