global ps2_init

PS2_DATA equ 0x60
PS2_STATUS equ 0x64
PS2_COMMAND equ 0x64

PS2_STATUS_OUTPUT_BUFFER_FULL equ 1 << 0
PS2_STATUS_INPUT_BUFFER_FULL equ 1 << 1

PS2_COMMAND_CCB_READ equ 0x20
PS2_COMMAND_CCB_WRITE equ 0x60
PS2_COMMAND_PORT_2_DISABLE equ 0xA7
PS2_COMMAND_PORT_2_ENABLE equ 0xA8
PS2_COMMAND_PORT_1_DISABLE equ 0xAD
PS2_COMMAND_PORT_1_ENABLE equ 0xAE
PS2_COMMAND_WRITE_TO_PORT_2 equ 0xD4

PS2_CCB_PORT_1_INTERRUPT equ 1 << 0
PS2_CCB_PORT_2_INTERRUPT equ 1 << 1
PS2_CCB_PORT_1_TRANSLATION equ 1 << 6

KEYBOARD_RESET equ 0xFF
KEYBOARD_ENABLE_SCAN equ 0xF6

MOUSE_RESET equ 0xFF
MOUSE_ENABLE_STREAMING equ 0xF4

MOUSE_RESET_ACK equ 0xAA

; Wait for the PS/2 output buffer to become full
; This function must be called before every read from the PS/2 data port.
ps2_wait_for_read:
  in al, PS2_STATUS
  test al, PS2_STATUS_OUTPUT_BUFFER_FULL
  jz ps2_wait_for_read
  ret

; Wait for the PS/2 input buffer to become empty
; This function must be called before every write to the PS/2 data or command port.
ps2_wait_for_write:
  in al, PS2_STATUS
  test al, PS2_STATUS_INPUT_BUFFER_FULL
  jnz ps2_wait_for_write
  ret

; Flush the PS/2 output buffer
; This function several times in the initialization sequence to make sure there isn't any data blocking the data port.
ps2_flush_buffer:
  in al, PS2_STATUS
  test al, PS2_STATUS_OUTPUT_BUFFER_FULL
  jz .end
  in al, PS2_DATA
  jmp ps2_flush_buffer
.end:
  ret

; Initialize the PS/2 controller
ps2_init:
  ; Disable both PS/2 ports before initialization
  call ps2_wait_for_write
  mov al, PS2_COMMAND_PORT_1_DISABLE
  out PS2_COMMAND, al
  call ps2_wait_for_write
  mov al, PS2_COMMAND_PORT_2_DISABLE
  out PS2_COMMAND, al
  call ps2_flush_buffer
  ; Read the Controller Configuration Byte
  call ps2_wait_for_write
  mov al, PS2_COMMAND_CCB_READ
  out PS2_COMMAND, al
  call ps2_wait_for_read
  in al, PS2_DATA
  ; Enable interrupts and disable keycode translation
  or al, PS2_CCB_PORT_1_INTERRUPT | PS2_CCB_PORT_2_INTERRUPT
  and al, ~PS2_CCB_PORT_1_TRANSLATION
  ; Write back the Controller Configuration Byte
  mov dl, al
  call ps2_wait_for_write
  mov al, PS2_COMMAND_CCB_WRITE
  out PS2_COMMAND, al
  call ps2_wait_for_write
  mov al, dl
  out PS2_DATA, al
  ; Enable keyboard port
  call ps2_wait_for_write
  mov al, PS2_COMMAND_PORT_1_ENABLE
  out PS2_COMMAND, al
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
  ; Disable keybaord port to avoid interference with mouse initialization
  call ps2_wait_for_write
  mov al, PS2_COMMAND_PORT_1_DISABLE
  out PS2_COMMAND, al
  ; Enable mouse port
  call ps2_wait_for_write
  mov al, PS2_COMMAND_PORT_2_ENABLE
  out PS2_COMMAND, al
  call ps2_flush_buffer
  ; Reset mouse
  call ps2_wait_for_write
  mov al, PS2_COMMAND_WRITE_TO_PORT_2
  out PS2_COMMAND, al
  call ps2_wait_for_write
  mov al, MOUSE_RESET
  out PS2_DATA, al
  ; Wait for mouse to send acknowledgement byte after reset
.wait_for_reset
  call ps2_wait_for_read
  in al, PS2_DATA
  cmp al, MOUSE_RESET_ACK
  jnz .wait_for_reset
  ; Enable mouse streaming
  call ps2_wait_for_write
  mov al, PS2_COMMAND_WRITE_TO_PORT_2
  out PS2_COMMAND, al
  call ps2_wait_for_write
  mov al, MOUSE_ENABLE_STREAMING
  out PS2_DATA, al
  ; Re-enable keyboard port
  call ps2_wait_for_write
  mov al, PS2_COMMAND_PORT_1_ENABLE
  out PS2_COMMAND, al
  call ps2_flush_buffer
  ret
