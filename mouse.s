global mouse_init

extern ps2_wait_for_read
extern ps2_wait_for_write
extern ps2_wait_for_write_to_port_2

PS2_DATA equ 0x60

MOUSE_RESET equ 0xFF
MOUSE_ENABLE_STREAMING equ 0xF4

MOUSE_RESET_ACK equ 0xAA

mouse_init:
  ; Reset mouse
  call ps2_wait_for_write_to_port_2
  mov al, MOUSE_RESET
  out PS2_DATA, al
  ; Wait for mouse to send acknowledgement byte after reset
.wait_for_reset:
  call ps2_wait_for_read
  in al, PS2_DATA
  cmp al, MOUSE_RESET_ACK
  jnz .wait_for_reset
  ; Enable mouse streaming
  call ps2_wait_for_write_to_port_2
  mov al, MOUSE_ENABLE_STREAMING
  out PS2_DATA, al
  ret
