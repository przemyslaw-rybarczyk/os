global pit_wait

PIT_CHANNEL_0_DATA equ 0x40
PIT_MODE_COMMAND equ 0x43

PIT_ACCESS_LO_HI equ 3 << 4
PIT_MODE_0 equ 0 << 1

; Wait a given number of PIT cycles, plus one (expressed as a u16)
; May work incorrectly if called with large values (~ above 0x8000).
pit_wait_u16:
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
  mov cx, dx
  in al, PIT_CHANNEL_0_DATA
  movzx dx, al
  in al, PIT_CHANNEL_0_DATA
  shl ax, 8
  or dx, ax
  cmp dx, di
  jbe .wait
  ret

; Wait a given number of PIT cycles (expressed as a u32)
pit_wait:
  push rdi
  push rbx
  mov ebx, edi
  shr ebx, 15
  ; Wait 2^15 cycles n/(2^15) times
.loop:
  test ebx, ebx
  jz .loop_end
  mov di, 0x7FFF
  call pit_wait_u16
  sub ebx, 1
  jmp .loop
.loop_end:
  ; Wait for the remaining n%(2^15) cycles
  pop rbx
  pop rdi
  and di, 0x7FFF
  test di, di
  jz .end
  sub di, 1
  jmp pit_wait_u16
.end:
  ret
