global time_get
global time_init
global time_adjust_offset
global start_interrupt_timer
global disable_interrupt_timer
global tsc_past_deadline

extern pit_wait
extern convert_time_from_rtc
extern timeslice_length

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

; The clock time at TSC equal to zero
tsc_offset: resq 1

section .text

; Return current time
time_get:
  ; Read TSC
  rdtsc
  shl rdx, 32
  or rax, rdx
  ; Multiply by TICKS_PER_TSC_CALIBRATION / tsc_frequency to get time in ticks
  mov rcx, TICKS_PER_TSC_CALIBRATION
  mul rcx
  div qword [tsc_frequency]
  add rax, [tsc_offset]
  ret

; Calibrate the TSC's frequency and offset
time_init:
  ; Calculate the frequency
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
  ; Set timeslice length for scheduler to tsc_frequency * TICKS_PER_TIMESLICE / TICKS_PER_TSC_CALIBRATION
  mov rax, TICKS_PER_TIMESLICE
  mul qword [tsc_frequency]
  mov rcx, TICKS_PER_TSC_CALIBRATION
  div rcx
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
  mov rcx, rax
  rdtsc
  shl rdx, 32
  or rax, rdx
  sub rcx, rax
  mov [tsc_offset], rcx
  ret

; Add current TSC value to TSC offset
; Called before resetting TSC to zero.
time_adjust_offset:
  rdtsc
  shl rdx, 32
  or rax, rdx
  add [tsc_offset], rax
  ret

; Arm the local APIC timer with a given TSC deadline
start_interrupt_timer:
  mov eax, edi
  shr rdi, 32
  mov edx, edi
  mov ecx, MSR_TSC_DEADLINE
  wrmsr
  ret

; Disarm the local APIC timer
disable_interrupt_timer:
  xor eax, eax
  xor edx, edx
  mov ecx, MSR_TSC_DEADLINE
  wrmsr
  ret

; Check if TSC is past the TSC deadline
tsc_past_deadline:
  ; Read the TSC
  rdtsc
  mov edi, eax
  shl rdx, 32
  or rdi, rdx
  ; Read the TSC deadline
  mov ecx, MSR_TSC_DEADLINE
  rdmsr
  mov esi, eax
  shl rdx, 32
  or rsi, rdx
  ; Compare them
  cmp rdi, rsi
  setge al
  ret
