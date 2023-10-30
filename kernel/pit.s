global pit_init
global pit_wait

PIT_CHANNEL_0_DATA equ 0x40
PIT_MODE_COMMAND equ 0x43

PIT_ACCESS_LO_HI equ 3 << 4
PIT_MODE_0 equ 0 << 1
PIT_MODE_2 equ 2 << 1

; This is the period of the singal we want to receive as a multiple of the period of the input signal (which runs at 1.193182 MHz).
; We use a value that makes the period closest to 20 ms.
PIT_TIMER_RELOAD_VALUE equ 23864

; Wait a given number of PIT cycles
; Should only be called during initialization.
pit_wait:
  ; Set the mode to mode 0 - interrupt on terminal count
  mov al, PIT_ACCESS_LO_HI | PIT_MODE_0
  out PIT_MODE_COMMAND, al
  ; Set the reload value
  mov ax, di
  out PIT_CHANNEL_0_DATA, al
  shr ax, 8
  out PIT_CHANNEL_0_DATA, al
  ; Wait for the timer to underflow after counting down to zero
.wait:
  in al, PIT_CHANNEL_0_DATA
  movzx dx, al
  in al, PIT_CHANNEL_0_DATA
  shl ax, 8
  or dx, ax
  cmp dx, di
  jbe .wait
  ret

pit_init:
  ; Set the mode to mode 2 - rate generator
  mov al, PIT_ACCESS_LO_HI | PIT_MODE_2
  out PIT_MODE_COMMAND, al
  ; Set the reload value
  mov al, PIT_TIMER_RELOAD_VALUE & 0xFF
  out PIT_CHANNEL_0_DATA, al
  mov al, PIT_TIMER_RELOAD_VALUE >> 8
  out PIT_CHANNEL_0_DATA, al
  ret
