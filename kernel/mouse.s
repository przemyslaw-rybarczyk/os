global mouse_init

extern ps2_wait_for_read
extern ps2_wait_for_write
extern ps2_wait_for_write_to_port_2
extern ps2_flush_buffer

extern mouse_has_scroll_wheel

PS2_DATA equ 0x60

MOUSE_GET_MOUSEID equ 0xF2
MOUSE_SET_SAMPLE_RATE equ 0xF3
MOUSE_ENABLE_STREAMING equ 0xF4
MOUSE_RESET equ 0xFF

MOUSE_RESET_ACK equ 0xAA

; Set up the mouse state so that the next byte sets the sample rate
mouse_prepare_to_set_sample_rate:
  call ps2_wait_for_write_to_port_2
  mov al, MOUSE_SET_SAMPLE_RATE
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  call ps2_wait_for_write_to_port_2
  ret

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
  call ps2_flush_buffer
  ; Try to enable the scroll wheel by executing a special sequence of "set sample rate" commands
  call mouse_prepare_to_set_sample_rate
  mov al, 200
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  call mouse_prepare_to_set_sample_rate
  mov al, 100
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  call mouse_prepare_to_set_sample_rate
  mov al, 80
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  ; Check mouseID
  ; If it's 3 or greater, the mouse has scroll wheel support.
  call ps2_wait_for_write_to_port_2
  mov al, MOUSE_GET_MOUSEID
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  call ps2_wait_for_read
  in al, PS2_DATA
  cmp al, 3
  setae [mouse_has_scroll_wheel]
  ; Set the sample rate
  call mouse_prepare_to_set_sample_rate
  mov al, 200
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  ; Enable mouse streaming
  call ps2_wait_for_write_to_port_2
  mov al, MOUSE_ENABLE_STREAMING
  out PS2_DATA, al
  call ps2_wait_for_read
  in al, PS2_DATA
  ret
