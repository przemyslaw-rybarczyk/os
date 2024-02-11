%include "kernel/percpu.inc"

global apic_init
global smp_init
global smp_init_sync
global apic_eoi
global send_wakeup_ipi
global wakeup_ipi_handler
global send_halt_ipi
global halt_ipi_handler

extern cpus
extern cpu_num
extern lapic

extern pit_wait
extern time_from_tsc

LAPIC_ID_REGISTER equ 0x020
LAPIC_EOI_REGISTER equ 0x0B0
LAPIC_LOGICAL_DESTINATION_REGISTER equ 0x0D0
LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER equ 0x0F0
LAPIC_ERROR_STATUS_REGISTER equ 0x280
LAPIC_INTERRUPT_COMMAND_REGISTER_LOW equ 0x300
LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH equ 0x310
LAPIC_TIMER_REGISTER equ 0x320

LAPIC_LOGICAL_ID_OFFSET equ 24
LAPIC_ENABLE equ 1 << 8

LAPIC_TIMER_TSC_DEADLINE equ 2 << 17

ICR_DESTINATION_OFFSET equ 56
ICR_ALL_EXCLUDING_SELF equ 3 << 18
ICR_ASSERT equ 1 << 14
ICR_FIXED equ 0 << 8
ICR_INIT equ 5 << 8
ICR_SIPI equ 6 << 8

; Number of the page AP code will start executing at
; We set it to 0x08, so that AP initialization code can be placed at 0x8000, right after the bootloader.
SIPI_VECTOR equ 0x08

LAPIC_TIMER_VECTOR equ 0x20
INT_VECTOR_WAKEUP_IPI equ 0x2D
INT_VECTOR_HALT_IPI equ 0x2E
SPURIOUS_INTERRUPT_VECTOR equ 0x2F

MSR_TSC equ 0x10

section .bss

; Number of initialized CPUs
cpu_initialized_num: resq 1

; Number of CPUs that have received the halt IPI
cpu_halted_num: resq 1

; Variables written to by the TSC during TSC synchronization
; BSP's TSC offset
sync_bsp_tsc_offset: resq 1
; BSP's TSC value during synchronization
sync_bsp_tsc_value: resq 1
; Set to 1 after writing TSC offset and value
sync_bsp_tsc_written: resb 1

resb 3

halt_ipi_lock: resd 1

section .text

; Initialize the local APIC
; The argument is true if the current processor is the BSP and false otherwise.
apic_init:
  mov rax, [lapic]
  ; Get the LAPIC ID
  mov edx, [rax + LAPIC_ID_REGISTER]
  and edx, 0xFF000000
  mov gs:[PerCPU.lapic_id], edx
  ; Set logical APIC ID to 1 for APs and 3 for BSP
  test dil, dil
  jnz .cpu_is_bsp
  mov dword [rax + LAPIC_LOGICAL_DESTINATION_REGISTER], 1 << LAPIC_LOGICAL_ID_OFFSET
  jmp .logical_destination_set
.cpu_is_bsp:
  mov dword [rax + LAPIC_LOGICAL_DESTINATION_REGISTER], 3 << LAPIC_LOGICAL_ID_OFFSET
.logical_destination_set:
  ; Enable the LAPIC and set the spurious interrupt vector
  mov dword [rax + LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER], LAPIC_ENABLE | SPURIOUS_INTERRUPT_VECTOR
  ; Set the timer register
  mov dword [rax + LAPIC_TIMER_REGISTER], LAPIC_TIMER_TSC_DEADLINE | LAPIC_TIMER_VECTOR
  ret

; Number of PIT cycles in 10 ms
WAIT_BEFORE_SIPI_PIT_CYCLES equ 11932

smp_init:
  mov rax, [lapic]
  ; Send INIT IPI to every AP
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH], 0
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_LOW], ICR_ALL_EXCLUDING_SELF | ICR_ASSERT | ICR_INIT
  ; Wait before sending SIPI
  push rax
  mov edi, WAIT_BEFORE_SIPI_PIT_CYCLES
  call pit_wait
  pop rax
  ; Send SIPI to every AP
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH], 0
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_LOW], ICR_ALL_EXCLUDING_SELF | ICR_ASSERT | ICR_SIPI | SIPI_VECTOR
  ret

; Synchronize all CPUs after initialization and set their TSC offsets
; The argument is true if the current processor is the BSP and false otherwise.
smp_init_sync:
  lock add qword [cpu_initialized_num], 1
  mov rax, [cpu_num]
.wait:
  cmp [cpu_initialized_num], rax
  jne .wait
  ; Get TSC immediately after wait loop exits
  ; It will be used to synchronize TSC timers between cores.
  rdtsc
  shl rdx, 32
  or rax, rdx
  test dil, dil
  jz .not_bsp
  ; The CPU is the BSP - the TSC offset is already known, so we write it along with the TSC value to the synchronization variables
  mov rdx, gs:[PerCPU.tsc_offset]
  mov [sync_bsp_tsc_offset], rdx
  mov [sync_bsp_tsc_value], rax
  mov byte [sync_bsp_tsc_written], 1
  ret
.not_bsp:
  ; The CPU is an AP - wait for the BSP to write its TSC offset and value to the synchronization variables
.wait_for_bsp_tsc_sync:
  cmp byte [sync_bsp_tsc_written], 0
  jz .wait_for_bsp_tsc_sync
  ; Calculate the TSC offset as sync_bsp_tsc_offset + time_from_tsc(sync_bsp_tsc_value - [tsc value])
  mov rdi, [sync_bsp_tsc_value]
  sub rdi, rax
  ; If difference is negative, negate before and after calling time_from_tsc(), since it takes unsigned values
  js .sub_neg
  call time_from_tsc
  jmp .after_sub
.sub_neg:
  neg rdi
  call time_from_tsc
  neg rax
.after_sub:
  add rax, [sync_bsp_tsc_offset]
  mov gs:[PerCPU.tsc_offset], rax
  ret

; Signify an EOI to the LAPIC
apic_eoi:
  mov rax, [lapic]
  mov dword [rax + LAPIC_EOI_REGISTER], 0
  ret

; Send a wakeup IPI to a given CPU
send_wakeup_ipi:
  mov rax, [lapic]
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH], edi
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_LOW], ICR_ASSERT | ICR_FIXED | INT_VECTOR_WAKEUP_IPI
  ret

wakeup_ipi_handler:
  ; Reset the idle flag
  mov byte gs:[PerCPU.idle], 0
  call apic_eoi
  ret

send_halt_ipi:
  ; Try to acquire the halt IPI lock
  mov edx, 1
  xor eax, eax
  lock cmpxchg [halt_ipi_lock], edx
  ; If another process has already acquired the lock, jump straight to the halt IPI handler
  ; This avoids a race condition when two cores encounter a fatal error at around the same time.
  jne halt_ipi_handler
  mov rax, [lapic]
  ; Send halt IPI to every other processor
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH], 0
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_LOW], ICR_ALL_EXCLUDING_SELF | ICR_ASSERT | ICR_FIXED | INT_VECTOR_HALT_IPI
  ; Increment the halted CPU counter and wait for all cores to be halted
  lock add qword [cpu_halted_num], 1
  mov rax, [cpu_num]
.wait:
  pause
  cmp [cpu_halted_num], rax
  jne .wait
  ret

halt_ipi_handler:
  ; After receiving a halt IPI, increment the halted CPU counter and halt
  lock add qword [cpu_halted_num], 1
.halt:
  hlt
  jmp .halt
