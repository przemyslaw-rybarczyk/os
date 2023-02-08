global keyboard_init

extern ps2_wait_for_read
extern ps2_wait_for_write

PS2_DATA equ 0x60

KEYBOARD_RESET equ 0xFF
KEYBOARD_ENABLE_SCAN equ 0xF6

keyboard_init:
  ; Reset keyboard
  call ps2_wait_for_write
  mov al, KEYBOARD_RESET
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  ; Enable keboard scanning
  call ps2_wait_for_write
  mov al, KEYBOARD_ENABLE_SCAN
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  ret
