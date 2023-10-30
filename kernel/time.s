global get_time
global tsc_calibrate

extern pit_wait

; We wait for around 10 ms to calibrate the TSC
TSC_CALIBRATE_PIT_CYCLES equ 11932
TICKS_PER_TSC_CALIBRATION equ 100002

section .bss

; The number of TSC cycles in a calibration period
tsc_frequency: resq 1

section .text

; Return current time
get_time:
  ; Read TSC
  rdtsc
  shl rdx, 32
  or rax, rdx
  ; Multiply by TICKS_PER_TSC_CALIBRATION / tsc_frequency to get time in ticks
  mov rcx, TICKS_PER_TSC_CALIBRATION
  mul rcx
  div qword [tsc_frequency]
  ret

; Calibrate the TSC to get its frequency
tsc_calibrate:
  ; Get first TSC reading
  rdtsc
  mov rcx, rax
  shl rdx, 32
  or rcx, rdx
  ; Wait
  mov di, TSC_CALIBRATE_PIT_CYCLES
  call pit_wait
  ; Get second TSC reading
  rdtsc
  shl rdx, 32
  or rax, rdx
  ; The difference between the two readings is the frequency
  sub rax, rcx
  mov [tsc_frequency], rax
  ret
