global pic_disable

PIC1_COMMAND equ 0x20
PIC1_DATA equ 0x21
PIC2_COMMAND equ 0xA0
PIC2_DATA equ 0xA1

pic_disable:
  mov al, 0xFF
  out PIC1_DATA, al
  out PIC2_DATA, al
  ret
