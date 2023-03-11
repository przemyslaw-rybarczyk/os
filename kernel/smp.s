global apic_init
global smp_init
global smp_init_sync_1
global smp_init_sync_2
global apic_eoi
global send_halt_ipi
global halt_ipi_handler

extern cpus
extern cpu_num
extern lapic

extern pit_wait_before_sipi

LAPIC_ID_REGISTER equ 0x020
LAPIC_EOI_REGISTER equ 0x0B0
LAPIC_LOGICAL_DESTINATION_REGISTER equ 0x0D0
LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER equ 0x0F0
LAPIC_ERROR_STATUS_REGISTER equ 0x280
LAPIC_INTERRUPT_COMMAND_REGISTER_LOW equ 0x300
LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH equ 0x310

LAPIC_LOGICAL_ID_OFFSET equ 24
LAPIC_ENABLE equ 1 << 8

ICR_ALL_EXCLUDING_SELF equ 3 << 18
ICR_ASSERT equ 1 << 14
ICR_FIXED equ 0 << 8
ICR_INIT equ 5 << 8
ICR_SIPI equ 6 << 8

; Number of the page AP code will start executing at
; We set it to 0x08, so that AP initialization code can be placed at 0x8000, right after the bootloader.
SIPI_VECTOR equ 0x08

INT_VECTOR_HALT_IPI equ 0x2E
SPURIOUS_INTERRUPT_VECTOR equ 0xFF

section .bss

; Number of initialized CPUs
cpu_initialized_num: resq 1
cpu_initialized_num_2: resq 1

; Number of CPUs that have received the halt IPI
cpu_halted_num: resq 1

halt_ipi_lock: resd 1

section .text

apic_init:
  mov rax, [lapic]
  ; Set logical APIC ID to 1
  mov dword [rax + LAPIC_LOGICAL_DESTINATION_REGISTER], 1 << LAPIC_LOGICAL_ID_OFFSET
  ; Enable the LAPIC and set the spurious interrupt vector
  mov dword [rax + LAPIC_SPURIOUS_INTERRUPT_VECTOR_REGISTER], LAPIC_ENABLE | SPURIOUS_INTERRUPT_VECTOR
  ret

smp_init:
  mov rax, [lapic]
  ; Send INIT IPI to every AP
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH], 0
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_LOW], ICR_ALL_EXCLUDING_SELF | ICR_ASSERT | ICR_INIT
  ; Wait before sending SIPI
  push rax
  call pit_wait_before_sipi
  pop rax
  ; Send SIPI to every AP
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_HIGH], 0
  mov dword [rax + LAPIC_INTERRUPT_COMMAND_REGISTER_LOW], ICR_ALL_EXCLUDING_SELF | ICR_ASSERT | ICR_SIPI | SIPI_VECTOR
  ret

; Synchronize all CPUs after initialization
smp_init_sync_1:
  lock add qword [cpu_initialized_num], 1
  mov rax, [cpu_num]
.wait:
  pause
  cmp [cpu_initialized_num], rax
  jne .wait
  ret

; Synchronize all CPUs after initialization again and invalidate the page tables
; This is an ugly to avoid dealing with page table changes properly and will be removed later.
smp_init_sync_2:
  lock add qword [cpu_initialized_num_2], 1
  mov rax, [cpu_num]
.wait:
  pause
  cmp [cpu_initialized_num_2], rax
  jne .wait
  mov rax, cr3
  mov cr3, rax
  ret

; Signify an EOI to the LAPIC
apic_eoi:
  mov rax, [lapic]
  mov dword [rax + LAPIC_EOI_REGISTER], 0
  ret

send_halt_ipi:
  ; Try to aquire the halt IPI lock
  mov edx, 1
  xor eax, eax
  lock cmpxchg [halt_ipi_lock], edx
  ; If another process has already acquired the lock, jump to .halt
  ; This avoids a race condition when two cores encounter a fatal error at around the same time.
  jne .halt
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
.halt:
  ; Halt with interrupts enabled
  ; Keeping interrupts enabled allows this thread to receive a halt IPI from another process
  sti
.halt_loop:
  hlt
  jmp .halt_loop

halt_ipi_handler:
  ; After receiving a halt IPI, increment the halted CPU counter and halt
  lock add qword [cpu_halted_num], 1
  cli
.halt:
  hlt
  jmp .halt
