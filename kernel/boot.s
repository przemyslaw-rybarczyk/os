extern kernel_start
extern kernel_start_ap

; Variables declared by linker script
extern KERNEL_LMA
extern kernel_sector_count
extern kernel_text_page_end
extern kernel_rodata_page_end
extern kernel_data_page_end
extern kernel_bss_lma_start
extern kernel_bss_length_dwords

global vbe_mode_info
global memory_ranges
global memory_ranges_length
global pd_fb
global pdpt_page_stack
global pd_devices_other
global pt_id_map_init
global idle_page_map

; Initial page for kernel stack
stack equ 0x7F000
stack_top equ stack + 0x1000

; Reserved memory for paging
pml4 equ 0x7E000
pdpt_id equ 0x7D000
pd_id equ 0x7C000
pdpt_stack equ 0x7B000
pd_stack equ 0x7A000
pt_stack equ 0x79000
pdpt_kernel equ 0x78000
pd_kernel equ 0x77000
pt_kernel equ 0x76000
pdpt_devices equ 0x75000
pd_fb equ 0x74000
pd_devices_other equ 0x73000
pt_id_map_init equ 0x72000
pdpt_page_stack equ 0x71000
boot_page_tables_start equ 0x71000
boot_page_tables_length equ 0x80000 - boot_page_tables_start

idle_page_map equ pml4

; Addresses of variables used by the bootloader

controller_info equ 0x0500
vbe_version equ controller_info + 0x04
video_modes_ptr equ controller_info + 0x0E

vbe_mode_info equ 0x0600
vbe_mode_res equ vbe_mode_info + 0x12
vbe_mode_bpp equ vbe_mode_info + 0x19
vbe_mode_memory_model equ vbe_mode_info + 0x1B

drive_index equ 0x07FC
memory_ranges_length equ 0x07FE

memory_ranges equ 0x0800
memory_ranges_end equ 0x7000

; BIOS constants

SMAP_MAGIC equ 'PAMS' ; in reverse because of endianness differences
MEMORY_RANGE_ACPI_ATTRS equ 20

VBE_MODE_ATTR_SUPPORTED equ 1 << 0
VBE_MODE_ATTR_COLOR equ 1 << 3
VBE_MODE_ATTR_GRAPHICS equ 1 << 4
VBE_MODE_ATTR_LINEAR_FB equ 1 << 7
VBE_MODE_DIRECT_COLOR equ 0x06
VBE_SET_LINEAR_FB equ 1 << 14

; CPU constants

CPUID_NX equ 1 << 20
CPUID_LONG_MODE equ 1 << 29
CPUID_SSE equ 1 << 25
CPUID_FXSR equ 1 << 24
CPUID_TSC equ 1 << 4
CPUID_INVARIANT_TSC equ 1 << 8
CPUID_TSC_DEADLINE equ 1 << 24
CR0_PE equ 1 << 0
CR0_MP equ 1 << 1
CR0_EM equ 1 << 2
CR0_PG equ 1 << 31
CR4_PAE equ 1 << 5
CR4_PGE equ 1 << 7
CR4_OSFXSR equ 1 << 9
CR4_OSXMMEXCPT equ 1 << 10
EFER_MSR equ 0xC0000080
EFER_MSR_SCE equ 1 << 0
EFER_MSR_LME equ 1 << 8
EFER_MSR_NXE equ 1 << 11

GDT_RW equ 1 << 1
GDT_EXECUTABLE equ 1 << 3
GDT_S equ 1 << 4
GDT_PRESENT equ 1 << 7
GDT_LONG_CODE equ 1 << 5
GDT_DB equ 1 << 6
GDT_GRANULAR equ 1 << 7

PAGE_PRESENT equ 1 << 0
PAGE_WRITE equ 1 << 1
PAGE_LARGE equ 1 << 7
PAGE_GLOBAL equ 1 << 8
PAGE_NX equ 1 << 63

PAGE_SIZE equ 1 << 12

; Kernel constants

; Maximum number of sectors that can reliably be read at once
SECTORS_PER_LOAD equ 0x7F

DEVICES_PML4E equ 0x1FD
FB_PDPTE equ 0x000
DEVICES_OTHER_PDPTE equ 0x002
STACK_PML4E equ 0x1FE
STACK_BOTTOM_VIRTUAL equ (0xFFFF << 48) | (STACK_PML4E << 39) | PAGE_SIZE
PAGE_STACK_PML4E equ 0x1FC

SEGMENT_KERNEL_CODE equ 0x08
SEGMENT_KERNEL_DATA equ 0x10

section .boot

; Error codes:
; 0 - Long mode not supported
; 1 - Failed to enable A20 gate
; 2 - Failed to detect memory
; 3 - INT 13h extensions not supported
; 4 - Failed to load kernel
; 5 - Failed to get controller info
; 6 - VBE version is less than 2.0
; 7 - Failed to find appropriate video mode
; 8 - Failed to set video mode
; 9 - No NX bit
; A - No SSE support
; B - No FXSAVE/FSRSTOR support
; C - No invariant TSC
; D - No TSC-Deadline mode

bits 16

start:
  ; Set data segment registers
  xor ax, ax
  mov ds, ax
  mov es, ax
  mov ss, ax
  ; Place stack right below loaded code
  mov sp, 0x7C00
  ; Clear carry - it may be set by later operations
  clc
  ; Store drive index in memory
  mov [drive_index], dl

; Check if long mode and NX bit is available
test_cpuid:
  ; CPUID EAX=80000000h - Get Highest Extended Function Implemented
  ; Check if CPUID EAX=80000001h can be called - if not, long mode is not available.
  mov eax, 0x80000000
  cpuid
  cmp eax, 0x80000001
  jb .no_long_mode
  ; If CPUID EAX=80000007h can't be called, invariant TSC is not available.
  cmp eax, 0x80000007
  jb .no_tsc
  ; CPUID EAX=80000001h - Extended Processor Info and Feature Bits
  ; Each feature has a corresponding bit set if it's available.
  mov eax, 0x80000001
  cpuid
  test edx, CPUID_LONG_MODE
  jz .no_long_mode
  test edx, CPUID_NX
  jz .no_nx
  ; CPUID EAX=1h - Processor Info and Feature Bits
  mov eax, 1
  cpuid
  test edx, CPUID_SSE
  jz .no_sse
  test edx, CPUID_FXSR
  jz .no_fxsr
  test edx, CPUID_TSC
  jz .no_tsc
  test ecx, CPUID_TSC_DEADLINE
  jz .no_tsc_deadline
  ; CPUID extended level EAX=80000007h
  mov eax, 0x80000007
  cpuid
  test edx, CPUID_INVARIANT_TSC
  jz .no_tsc
  jmp .end
.no_long_mode:
  mov dl, '0'
  jmp error
.no_nx:
  mov dl, '9'
  jmp error
.no_sse:
  mov dl, 'A'
  jmp error
.no_fxsr:
  mov dl, 'B'
  jmp error
.no_tsc:
  mov dl, 'C'
  jmp error
.no_tsc_deadline:
  mov dl, 'D'
  jmp error
.end:

; Enable the A20 gate
enable_a20_gate:
  ; INT 15h AX=2403h - Query A20 Gate Support
  ; If successful, CF is clear and AH=0
  mov ax, 0x2403
  int 0x15
  jc .fail
  test ah, ah
  jnz .fail
  ; INT 15h AX=2402h - Get A20 Gate Status
  ; If successful, CF is clear and AH=0
  ; AL is 1 if gate is enabled, 0 otherwise
  mov ax, 0x2402
  int 0x15
  jc .no_2402
  test ah, ah
  jnz .no_2402
  cmp al, 0x01
  je .end
.no_2402:
  ; INT 15h AX=2401h - Enable A20 Gate
  ; If successful, CF is clear and AH=0
  mov ax, 0x2401
  int 0x15
  jc .fail
  test ah, ah
  jz .end
.fail:
  mov dl, '1'
  jmp error
.end:

; Detect available memory
detect_memory:
  ; INT 15h, AX=E820h - Query System Address Map
  ; We call this function in a loop to get an array of structures describing the available memory ranges.
  ; EDX holds the magic value 'SMAP'.
  mov edx, SMAP_MAGIC
  ; EBX holds the continuation value. It is initially 0 and updated by BIOS after each call.
  mov ebx, 0
  ; ES:DI holds the address at which the range descriptor structure is to be written.
  ; We increment it after each call.
  mov di, memory_ranges
  ; SI holds the address to jump to if the function returns with carry set.
  ; Since the carry flag is used both to indicate both an error and the end of the list,
  ; we assume that if the first call returns with carry set it indicates an error, and the end otherwise.
  ; Therefore SI is set to .fail intially, but is changed to .success after the first call succeeds.
  mov si, .fail
.loop:
  ; Since the value stored by BIOS may be only 20 bytes long rather than the 24 we expect,
  ; we set the ACPI attributes to 3 for compatibility. This value indicates that the entry shouldn't be ignored.
  mov [di + MEMORY_RANGE_ACPI_ATTRS], dword 1
  mov eax, 0xE820
  ; ECX holds the size of the buffer.
  mov ecx, 24
  int 0x15
  ; If carry if set, we end the loop.
  jnc .no_carry
  jmp si
.no_carry:
  ; EAX should be set to the magic value 'SMAP' after the call.
  cmp eax, SMAP_MAGIC
  jne .fail
  mov si, .success
  add di, 24
  ; This is unlikely to happen, but if DI reaches the end of its buffer we end the loop prematurely to avoid overwriting other data.
  cmp di, memory_ranges_end
  jae .success
  ; If EBX is zero, we've just read the final memory range.
  test ebx, ebx
  jnz .loop
.success:
  ; Store the length of the address map buffer in memory.
  sub di, memory_ranges
  mov [memory_ranges_length], di
  jmp .end
.fail:
  mov dl, '2'
  jmp error
.end:

; Load the kernel
load_kernel:
  ; INT 13h, AH=41h - Check Extensions Present
  ; CF will be set if INT 13h extensions are not present.
  mov ah, 0x41
  mov bx, 0x55AA
  mov dl, 0x80
  int 0x13
  jnc .int13_supported
  mov dl, '3'
  jmp error
.int13_supported:
  ; Since we can only reliably load 127 sectors at once, we have to make multiple BIOS calls.
  ; We first loop, loading 127 sectors in each iteration. After the loop the remaining sectors are loaded.
.loop:
  mov dx, [int13_dap.start]
  add dx, SECTORS_PER_LOAD
  cmp dx, kernel_sector_count
  jae .loop_end
  ; INT 13h AH=42h - Extended Read Sectors From Drive
  ; Load the rest of the kernel at 0x7E00, right after the boot sector.
  ; DL holds index of drive to read from. SI holds pointer to the Disk Address Packet.
  ; On success, CF will be clear and AH set to 0.
  mov dl, [drive_index]
  mov ah, 0x42
  mov si, int13_dap
  int 0x13
  jc .fail
  test ah, ah
  jnz .fail
  add word [int13_dap.segment], SECTORS_PER_LOAD * 32
  add word [int13_dap.start], SECTORS_PER_LOAD
  jmp .loop
.loop_end:
  mov dx, kernel_sector_count
  sub dx, [int13_dap.start]
  mov [int13_dap.sectors], dx
  mov dl, [drive_index]
  mov ah, 0x42
  mov si, int13_dap
  int 0x13
  jc .fail
  test ah, ah
  jnz .fail
  jmp .end
.fail:
  mov dl, '4'
  jmp error
.end:
  jmp get_video_mode

; Disk Address Packet
; The segment is set so that the offset is 0.
; This allows loading up to 64 KiB, as the entire load buffer must fit inside a single segment.
; Since the boot sector is already loaded, we start loading from sector 1.
align 4
int13_dap:
  db 0x10 ; size of DAP
  db 0 ; unused
.sectors:
  dw SECTORS_PER_LOAD ; number of sectors to read
.offset:
  dw 0x0000 ; offset of load buffer
.segment:
  dw 0x07E0 ; segment of load buffer
.start:
  dq 1 ; first sector to load

error_msg: db `Error \0`

; Halts with error code in dl displayed
error:
  cld
  mov si, error_msg
  mov ah, 0x0E
.loop:
  lodsb
  test al, al
  jz .end
  int 0x10
  jmp .loop
.end:
  mov al, dl
  int 0x10
.halt:
  cli
  hlt
  jmp .halt

; Fill the rest of the boot sector with zeroes and place the boot signature at the end.
times 0x1FE - ($-$$) db 0
dw 0xAA55

; Go through the available video modes and find the best one
; We use VBE 2.0 functions for this. Each one is called with INT 10h AH=4Fh and AL set to the function number.
; If the call is successful, the function will return with AX set to 004Fh.
get_video_mode:
  ; VBE Function 00h - Return VBE Controller Information
  ; DI holds address of 256-byte buffer to store the controller info.
  ; The information starts with the signature and version number,
  ; which we compare to 'VESA' and 0x0200 to check if the VBE version is at least 2.0.
  mov ax, 0x4F00
  mov di, controller_info
  int 0x10
  mov dl, '5'
  cmp ax, 0x004F
  jne error
  cmp dword [controller_info], 'VESA'
  jne error
  mov dl, '6'
  cmp word [vbe_version], 0x0200
  jb error
  ; Now we loop through the video available video modes to find the best one.
  ; They are held in a list, the pointer to which is found in the controller info.
  ; An element of 0xFFFF indicates the end of the list.
  cld
  ; FS:SI holds the pointer to the next video mode.
  mov fs, [video_modes_ptr + 2]
  mov si, [video_modes_ptr]
  ; BP holds the number of the best supported mode.
  ; We use 0xFFFF as a default. Since it acts as a terminator, it's guaranteed to not be a valid mode.
  mov bp, 0xFFFF
  ; EDX holds the resolution of the best mode.
  xor edx, edx
  ; BL holds the bits per pixel of the best mode.
  xor bl, bl
.loop:
  ; Read the next mode number into AX.
  mov ax, fs:[si]
  add si, 2
  ; If it's 0xFFFF, we've reached the end.
  cmp ax, 0xFFFF
  je .loop_end
  ; VBE Function 01h - Return VBE Mode Information
  ; CX holds the mode number.
  ; DI holds address of 256-byte buffer to store mode info.
  ; We store bp on the stack because some BIOSes may overwrite it.
  push bp
  mov cx, ax
  mov ax, 0x4F01
  mov di, vbe_mode_info
  int 0x10
  pop bp
  cmp ax, 0x004F
  jne .loop
  ; The first word of the mode info structure contains the attributes.
  ; We require some of the attributes to be set - otherwise we skip the mode.
  mov eax, [vbe_mode_info]
  not eax
  test eax, VBE_MODE_ATTR_SUPPORTED | VBE_MODE_ATTR_COLOR | VBE_MODE_ATTR_GRAPHICS | VBE_MODE_ATTR_LINEAR_FB
  jnz .loop
  ; Check if the memory model is direct color.
  cmp byte [vbe_mode_memory_model], VBE_MODE_DIRECT_COLOR
  jne .loop
  ; We require the bits per pixel to be a multiple of 8 not greater than 32 (that is, either 8, 16, 24, or 32).
  mov al, [vbe_mode_bpp]
  ror al, 3
  cmp al, 4
  ja .loop
  ; Compare bits per pixel to that of the current best mode.
  mov al, [vbe_mode_bpp]
  cmp al, bl
  jb .loop
  ja .found_new_best_mode
  ; Get the mode's resolution and compare it to that of the current best mode.
  mov eax, [vbe_mode_res]
  cmp eax, edx
  jb .loop
.found_new_best_mode:
  ; If we reach this point, we have found the new best mode.
  ; Update all the registers to reflect this fact.
  mov bp, cx
  mov edx, [vbe_mode_res]
  mov bl, [vbe_mode_bpp]
  jmp .loop
.loop_end:
  ; If BP is still the defualt value then we haven't found a single appropriate mode.
  mov dl, '7'
  cmp bp, 0xFFFF
  je error
  ; After finding the best mode, we load its mode information for the kernel to use.
  mov cx, bp
  mov ax, 0x4F01
  mov di, vbe_mode_info
  int 0x10
  mov dl, '7'
  cmp ax, 0x004F
  jne error
.got_vbe_mode_info:
  ; VBE Function 02h - Set VBE Mode
  ; BX holds the mode number together with some option bits.
  ; We set the bit to get a linear framebuffer.
  mov bx, cx
  or bx, VBE_SET_LINEAR_FB
  mov ax, 0x4F02
  int 0x10
  cmp ax, 0x004F
  mov dl, '8'
  jne error
.end:
  jmp enter_protected_mode

; Global Descriptor Table for use in protected mode
; The only difference is that the flags in the kernel code segment field has the DB flag set instead of the L flag.
align 8
gdt32:
  ; Entry 0x00 - unused
  dq 0
  ; Entry 0x08 - kernel code
  dw 0xFFFF ; limit bits 0-15
  dw 0x0000 ; base bits 0-15
  db 0x00 ; base bits 16-23
  db GDT_PRESENT | GDT_S | GDT_EXECUTABLE | GDT_RW ; access byte
  db GDT_DB | GDT_GRANULAR | 0xF ; flags and limit bits 16-19
  db 0x00 ; base bits 24-31
  ; Entry 0x10 - kernel data
  dw 0xFFFF ; limit bits 0-15
  dw 0x0000 ; base bits 0-15
  db 0x00 ; base bits 16-23
  db GDT_PRESENT | GDT_S | GDT_RW ; access byte
  db GDT_DB | GDT_GRANULAR | 0xF ; flags and limit bits 16-19
  db 0x00 ; base bits 24-31
gdt32_length equ $ - gdt32

; Global Descriptor Table
align 8
gdt:
  ; Entry 0x00 - unused
  dq 0
  ; Entry 0x08 - kernel code
  dw 0xFFFF ; limit bits 0-15
  dw 0x0000 ; base bits 0-15
  db 0x00 ; base bits 16-23
  db GDT_PRESENT | GDT_S | GDT_EXECUTABLE | GDT_RW ; access byte
  db GDT_LONG_CODE | GDT_GRANULAR | 0xF ; flags and limit bits 16-19
  db 0x00 ; base bits 24-31
  ; Entry 0x10 - kernel data
  dw 0xFFFF ; limit bits 0-15
  dw 0x0000 ; base bits 0-15
  db 0x00 ; base bits 16-23
  db GDT_PRESENT | GDT_S | GDT_RW ; access byte
  db GDT_DB | GDT_GRANULAR | 0xF ; flags and limit bits 16-19
  db 0x00 ; base bits 24-31
gdt_length equ $ - gdt

; 32-bit GDT Descriptor for use in protected mode
gdtr32:
  dw gdt32_length - 1
  dd gdt32

; 64-bit GDT Descriptor for use in long mode
gdtr:
  dw gdt_length - 1
  dq gdt

enter_protected_mode:
  ; Load GDT
  lgdt [gdtr32]
  ; Set PE in CR0
  mov eax, cr0
  or eax, CR0_PE
  mov cr0, eax
  ; Jump to enter protected mode
  jmp SEGMENT_KERNEL_CODE:protected_mode_start

; Pad so that AP initialization code starts at 0x8000
times 0x400 - ($-$$) db 0

bits 16

start_ap:
  cli
  ; Set data segment register
  mov ax, 0
  mov ds, ax
  ; Load GDT
  lgdt [gdtr32]
  ; Set PE in CR0
  mov eax, cr0
  or eax, CR0_PE
  mov cr0, eax
  ; Jump to enter protected mode
  jmp SEGMENT_KERNEL_CODE:protected_mode_start_ap

bits 32

; Set up paging and enter long mode
protected_mode_start_ap:
  ; Enable PAE, PGE, OSFXSR, and OSXMMEXCPT in CR4
  mov eax, cr4
  or eax, CR4_PAE | CR4_PGE | CR4_OSFXSR | CR4_OSXMMEXCPT
  mov cr4, eax
  ; Set CR3 to address of PML4
  mov eax, pml4
  mov cr3, eax
  ; Set SCE, LME, and NXE in EFER MSR
  mov ecx, EFER_MSR
  rdmsr
  or eax, EFER_MSR_SCE | EFER_MSR_LME | EFER_MSR_NXE
  wrmsr
  ; Set PG and clear EM in CR0
  mov eax, cr0
  or eax, CR0_PG | CR0_MP
  and eax, ~CR0_EM
  mov cr0, eax
  ; Load GDT
  lgdt [gdtr]
  ; Jump to enter long mode
  jmp SEGMENT_KERNEL_CODE:long_mode_start_ap

bits 64

align 8
next_ap_id: dq 0

long_mode_start_ap:
  ; Set data segment registers
  mov ax, SEGMENT_KERNEL_DATA
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax
  ; Load initial kernel stack
  mov rdi, 1
  lock xadd [next_ap_id], rdi
  mov rsi, STACK_BOTTOM_VIRTUAL + 2 * PAGE_SIZE
  mov rax, rdi
  shl rax, 13
  add rsi, rax
  mov rsp, rsi
  ; Finally, enter the kernel
  call kernel_start_ap
  ; Loop forever if kernel exits - this shouldn't happen
.halt:
  cli
  hlt
  jmp .halt

bits 32

; Set up paging and enter long mode
protected_mode_start:
  cli
  ; Set data segment registers
  mov ax, SEGMENT_KERNEL_DATA
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax
  ; Clear page tables
  cld
  xor eax, eax
  mov edi, boot_page_tables_start
  mov ecx, boot_page_tables_length >> 2
  rep stosd
  ; Set up basic paging
  ; Identity map the lowest 2 MiB with a large page
  mov dword [pml4], pdpt_id | PAGE_WRITE | PAGE_PRESENT
  mov dword [pdpt_id], pd_id | PAGE_WRITE | PAGE_PRESENT
  mov dword [pd_id], 0 | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT
  ; Map bottom of stack to first page in PML4E number STACK_PML4E
  mov dword [pml4 + STACK_PML4E * 8], pdpt_stack | PAGE_WRITE | PAGE_PRESENT
  mov dword [pdpt_stack], pd_stack | PAGE_WRITE | PAGE_PRESENT
  mov dword [pd_stack], pt_stack | PAGE_WRITE | PAGE_PRESENT
  mov dword [pt_stack], stack | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT
  mov dword [pt_stack + 4], PAGE_NX >> 32
  ; Set up mapping for framebuffer, identity mapping initialization and page stack
  ; The pd_fb, pt_id_map_init, and pdpt_page_stack will be filled in by the kernel.
  mov dword [pml4 + DEVICES_PML4E * 8], pdpt_devices | PAGE_WRITE | PAGE_PRESENT
  mov dword [pdpt_devices + FB_PDPTE * 8], pd_fb | PAGE_WRITE | PAGE_PRESENT
  mov dword [pdpt_devices + DEVICES_OTHER_PDPTE * 8], pd_devices_other | PAGE_WRITE | PAGE_PRESENT
  mov dword [pd_devices_other], pt_id_map_init | PAGE_WRITE | PAGE_PRESENT
  mov dword [pml4 + PAGE_STACK_PML4E * 8], pdpt_page_stack | PAGE_WRITE | PAGE_PRESENT
  mov dword [pml4 + PAGE_STACK_PML4E * 8 + 4], PAGE_NX >> 32
  ; Map kernel contents at the beginning of the last PDPTE (top 1 GB of address space)
  ; Each segment is mapped with the appropriate permissions.
  mov dword [pml4 + 0x1FF * 8], pdpt_kernel | PAGE_WRITE | PAGE_PRESENT
  mov dword [pdpt_kernel + 0x1FF * 8], pd_kernel | PAGE_WRITE | PAGE_PRESENT
  mov dword [pd_kernel], pt_kernel | PAGE_WRITE | PAGE_PRESENT
  mov eax, KERNEL_LMA + (PAGE_GLOBAL | PAGE_LARGE | PAGE_PRESENT)
  mov ecx, 0
.text_loop:
  cmp ecx, kernel_text_page_end
  jae .text_loop_end
  mov dword [pt_kernel + ecx * 8], eax
  add eax, 0x1000
  add ecx, 1
  jmp .text_loop
.text_loop_end:
.rodata_loop:
  cmp ecx, kernel_rodata_page_end
  jae .rodata_loop_end
  mov dword [pt_kernel + ecx * 8], eax
  mov dword [pt_kernel + ecx * 8 + 4], PAGE_NX >> 32
  add eax, 0x1000
  add ecx, 1
  jmp .rodata_loop
.rodata_loop_end:
  or eax, PAGE_WRITE
.data_loop:
  cmp ecx, kernel_data_page_end
  jae .data_loop_end
  mov dword [pt_kernel + ecx * 8], eax
  mov dword [pt_kernel + ecx * 8 + 4], PAGE_NX >> 32
  add eax, 0x1000
  add ecx, 1
  jmp .data_loop
.data_loop_end:
  ; Clear .bss
  xor eax, eax
  mov edi, kernel_bss_lma_start
  mov ecx, kernel_bss_length_dwords
  rep stosd

  ; Enable PAE, PGE, OSFXSR, and OSXMMEXCPT in CR4
  mov eax, cr4
  or eax, CR4_PAE | CR4_PGE | CR4_OSFXSR | CR4_OSXMMEXCPT
  mov cr4, eax
  ; Set CR3 to address of PML4
  mov eax, pml4
  mov cr3, eax
  ; Set SCE, LME, and NXE in EFER MSR
  mov ecx, EFER_MSR
  rdmsr
  or eax, EFER_MSR_SCE | EFER_MSR_LME | EFER_MSR_NXE
  wrmsr
  ; Set PG and clear EM in CR0
  mov eax, cr0
  or eax, CR0_PG | CR0_MP
  and eax, ~CR0_EM
  mov cr0, eax
  ; Load GDT
  lgdt [gdtr]
  ; Jump to enter long mode
  jmp SEGMENT_KERNEL_CODE:long_mode_start

bits 64

long_mode_start:
  ; Set data segment registers
  mov ax, SEGMENT_KERNEL_DATA
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov ss, ax
  ; Set rsp to bottom of stack
  mov rsp, STACK_BOTTOM_VIRTUAL
  ; Finally, enter the kernel
  mov rdi, rsp
  call kernel_start
  ; Loop forever if kernel exits - this shouldn't happen
.halt:
  cli
  hlt
  jmp .halt
