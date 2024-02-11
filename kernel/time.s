%include "kernel/percpu.inc"

global time_init
global time_get_tsc
global time_from_tsc
global timestamp_to_tsc
global time_get
global start_interrupt_timer
global disable_interrupt_timer
global tsc_past_deadline

extern pit_wait
extern convert_time_from_rtc
extern timeslice_length
extern preempt_disable
extern preempt_enable

; We wait for around 10 ms to calibrate the TSC
TSC_CALIBRATE_PIT_CYCLES equ 11932
TICKS_PER_TSC_CALIBRATION equ 100002

; Set the timeslice length to 20ms
TICKS_PER_TIMESLICE equ 200000

CMOS_ADDR equ 0x70
CMOS_DATA equ 0x71

CMOS_YEAR equ 0x09
CMOS_MONTH equ 0x08
CMOS_DAY equ 0x07
CMOS_HOUR equ 0x04
CMOS_MINUTE equ 0x02
CMOS_SECOND equ 0x00
CMOS_STATUS_B equ 0x0B

MSR_TSC_DEADLINE equ 0x6E0

section .bss

; The number of TSC cycles in a calibration period
tsc_frequency: resq 1

section .text

; Calibrate the TSC's frequency and offset
time_init:
  ; Calculate the frequency
  ; Get first TSC reading
  call time_get_tsc
  mov rcx, rax
  ; Wait
  mov edi, TSC_CALIBRATE_PIT_CYCLES
  call pit_wait
  ; Get second TSC reading
  push rcx
  call time_get_tsc
  pop rcx
  ; The difference between the two readings is the frequency
  sub rax, rcx
  mov [tsc_frequency], rax
  ; Set timeslice length for scheduler
  mov rdi, TICKS_PER_TIMESLICE
  call time_to_tsc
  mov [timeslice_length], rax
  ; Calculate the offset
  xor rdx, rdx
.loop:
  ; Read year:month:day:hour:minute:second (6 bytes, concatenated) into RDI
  mov rax, CMOS_YEAR
  out CMOS_ADDR, al
  in al, CMOS_DATA
  mov rdi, rax
  mov al, CMOS_MONTH
  out CMOS_ADDR, al
  in al, CMOS_DATA
  shl rdi, 8
  or rdi, rax
  mov al, CMOS_DAY
  out CMOS_ADDR, al
  in al, CMOS_DATA
  shl rdi, 8
  or rdi, rax
  mov al, CMOS_HOUR
  out CMOS_ADDR, al
  in al, CMOS_DATA
  shl rdi, 8
  or rdi, rax
  mov al, CMOS_MINUTE
  out CMOS_ADDR, al
  in al, CMOS_DATA
  shl rdi, 8
  or rdi, rax
  mov al, CMOS_SECOND
  out CMOS_ADDR, al
  in al, CMOS_DATA
  shl rdi, 8
  or rdi, rax
  ; Compare to old value in RDX
  ; If it's different, a partial update might have occurred between the two reads, so we try again.
  cmp rdi, rdx
  mov rdx, rdi
  jne .loop
  ; Once we have the time, convert it to a timestamp
  mov al, CMOS_STATUS_B
  out CMOS_ADDR, al
  in al, CMOS_DATA
  mov sil, al
  call convert_time_from_rtc
  ; Finally, subtract the current TSC value from the clock time to get the offset
  push rax
  call time_get_tsc
  mov rdi, rax
  call time_from_tsc
  pop rcx
  sub rcx, rax
  mov gs:[PerCPU.tsc_offset], rcx
  ret

; Return current TSC counter value
time_get_tsc:
  rdtsc
  shl rdx, 32
  or rax, rdx
  ret

; Convert time from TSC tick count
time_from_tsc:
  ; Multiply by TICKS_PER_TSC_CALIBRATION / tsc_frequency
  mov rax, rdi
  mov rcx, TICKS_PER_TSC_CALIBRATION
  mul rcx
  div qword [tsc_frequency]
  ret

; Convert time to TSC tick count
time_to_tsc:
  ; Multiply by tsc_frequency / TICKS_PER_TSC_CALIBRATION
  mov rax, rdi
  mul qword [tsc_frequency]
  mov rcx, TICKS_PER_TSC_CALIBRATION
  div rcx
  ret

; Convert timestamp to TSC tick count
; Out of range values are clamped to unsigned 64-bit range.
; Must be called with preemption disabled to work correctly.
timestamp_to_tsc:
  ; Subtract offset
  sub rdi, gs:[PerCPU.tsc_offset]
  ; Return 0 if result is negative
  jl .negative
  ; Multiply by tsc_frequency / TICKS_PER_TSC_CALIBRATION
  mov rax, rdi
  mul qword [tsc_frequency]
  mov rcx, TICKS_PER_TSC_CALIBRATION
  ; Return maximum 64-bit value if division would overflow
  cmp rdx, rcx
  jae .too_large
  div rcx
  ret
.negative:
  xor rax, rax
  ret
.too_large:
  mov rax, -1
  ret

; Return current time
time_get:
  call preempt_disable
  ; Read TSC
  call time_get_tsc
  mov rdi, rax
  ; Convert to ticks
  call time_from_tsc
  ; Add offset
  push rax
  call preempt_enable
  pop rax
  add rax, gs:[PerCPU.tsc_offset]
  ret

; Arm the local APIC timer with a given TSC deadline
start_interrupt_timer:
  mov gs:[PerCPU.tsc_deadline], rdi
  mov eax, edi
  shr rdi, 32
  mov edx, edi
  mov ecx, MSR_TSC_DEADLINE
  wrmsr
  ret

; Disarm the local APIC timer
disable_interrupt_timer:
  mov qword gs:[PerCPU.tsc_deadline], 0
  xor eax, eax
  xor edx, edx
  mov ecx, MSR_TSC_DEADLINE
  wrmsr
  ret

; Check if TSC is past the TSC deadline
tsc_past_deadline:
  ; Read the TSC
  call time_get_tsc
  ; Get the TSC deadline
  mov rsi, gs:[PerCPU.tsc_deadline]
  ; If the deadline is zero, return false since the timer is disarmed
  test rsi, rsi
  jnz .timer_armed
  xor al, al
  ret
.timer_armed:
  ; Compare them
  cmp rax, rsi
  setge al
  ret
