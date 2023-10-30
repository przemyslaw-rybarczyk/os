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

LAPIC_ID_REGISTER equ 0x020
LAPIC_EOI_REGISTER equ 0x0B0
LAPIC_LOGICAL_DESTINATION_REGISTER equ 0x0D0
LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER equ 0x0F0
LAPIC_ERROR_STATUS_REGISTER equ 0x280
LAPIC_INTERRUPT_COMMAND_REGISTER_LOW equ 0x300
LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH equ 0x310

LAPIC_LOGICAL_ID_OFFSET equ 24
LAPIC_ENABLE equ 1 << 8

ICR_DESTINATION_OFFSET equ 56
ICR_ALL_EXCLUDING_SELF equ 3 << 18
ICR_ASSERT equ 1 << 14
ICR_FIXED equ 0 << 8
ICR_INIT equ 5 << 8
ICR_SIPI equ 6 << 8

; Number of the page AP code will start executing at
; We set it to 0x08, so that AP initialization code can be placed at 0x8000, right after the bootloader.
SIPI_VECTOR equ 0x08

INT_VECTOR_WAKEUP_IPI equ 0x2D
INT_VECTOR_HALT_IPI equ 0x2E
SPURIOUS_INTERRUPT_VECTOR equ 0xFF

section .bss

; Number of initialized CPUs
cpu_initialized_num: resq 1

; Number of CPUs that have received the halt IPI
cpu_halted_num: resq 1

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
  test rdi, rdi
  jnz .cpu_is_bsp
  mov dword [rax + LAPIC_LOGICAL_DESTINATION_REGISTER], 1 << LAPIC_LOGICAL_ID_OFFSET
  jmp .logical_destination_set
.cpu_is_bsp:
  mov dword [rax + LAPIC_LOGICAL_DESTINATION_REGISTER], 3 << LAPIC_LOGICAL_ID_OFFSET
.logical_destination_set:
  ; Enable the LAPIC and set the spurious interrupt vector
  mov dword [rax + LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER], LAPIC_ENABLE | SPURIOUS_INTERRUPT_VECTOR
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
  mov di, WAIT_BEFORE_SIPI_PIT_CYCLES
  call pit_wait
  pop rax
  ; Send SIPI to every AP
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH], 0
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_LOW], ICR_ALL_EXCLUDING_SELF | ICR_ASSERT | ICR_SIPI | SIPI_VECTOR
  ret

; Synchronize all CPUs after initialization
smp_init_sync:
  lock add qword [cpu_initialized_num], 1
  mov rax, [cpu_num]
.wait:
  pause
  cmp [cpu_initialized_num], rax
  jne .wait
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
