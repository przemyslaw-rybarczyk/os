global pit_init

PIT_CHANNEL_0_DATA equ 0x40
PIT_MODE_COMMAND equ 0x43

PIT_ACCESS_LO_HI equ 3 << 4
PIT_MODE_2 equ 2 << 1

; This is the period of the singal we want to receive as a multiple of the period of the input signal (which runs at 1.193182 MHz).
; We use a value that makes the period closest to 1 ms.
PIT_TIMER_RELOAD_VALUE equ 1193

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
