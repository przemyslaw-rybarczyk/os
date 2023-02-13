extern kernel_main

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
pdpt_fb equ 0x75000
pd_fb equ 0x74000
pdpt_page_stack equ 0x73000
boot_page_tables_start equ 0x73000
boot_page_tables_length equ 0xC000

; Addresses of variables used by the bootloader

controller_info equ 0x0500
vbe_version equ controller_info + 0x04
video_modes_ptr equ controller_info + 0x0E

vbe_mode_info equ 0x0600
vbe_mode_res equ vbe_mode_info + 0x12
vbe_mode_bpp equ vbe_mode_info + 0x19
vbe_mode_memory_model equ vbe_mode_info + 0x21

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
CR0_PE equ 1 << 0
CR0_PG equ 1 << 31
CR4_PAE equ 1 << 5
CR4_PGE equ 1 << 7
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

; Kernel constants

RECURSIVE_PML4E equ 0x100
FB_PML4E equ 0x1FD
STACK_PML4E equ 0x1FE
STACK_BOTTOM_VIRTUAL equ (0xFFFF << 48) | ((STACK_PML4E + 1) << 39)
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

bits 16

  ; Set data segment registers
  mov ax, 0
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
  ; CPUID EAX=80000001h - Extended Processor Info and Feature Bits
  ; Each feature has a corresponding bit set if it's available.
  mov eax, 0x80000001
  cpuid
  test edx, CPUID_LONG_MODE
  jz .no_long_mode
  test edx, CPUID_NX
  jz .no_nx
  jmp .end
.no_long_mode:
  mov dl, '0'
  jmp error
.no_nx:
  mov dl, '9'
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
  mov [di + MEMORY_RANGE_ACPI_ATTRS], dword 3
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
  jz .end
.fail:
  mov dl, '4'
  jmp error
.end:

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
  ; SI holds the pointer to the next video mode.
  mov si, [video_modes_ptr]
  ; BP holds the number of the best supported mode.
  ; We use 0xFFFF as a default. Since it acts as a terminator, it's guaranteed to not be a valid mode.
  mov bp, 0xFFFF
  ; EDX holds the resolution of the best mode.
  ; The upper 16 bits hold the horizontal resolution and the lower 16 the vertical.
  ; This is done so the horizontal resolution takes priority in comparisons.
  xor edx, edx
  ; BL holds the bits per pixel of the best mode.
  xor bl, bl
.loop:
  ; Read the next mode number into AX.
  lodsw
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
  ; Get the mode's resolution and compare it to that of the current best mode.
  ; We swap the horizontal and vertical resolution so the horizontal resolution is the upper 16 bits.
  mov eax, [vbe_mode_res]
  ror eax, 16
  cmp eax, edx
  jb .loop
  ; Check if the memory model is direct color.
  cmp byte [vbe_mode_memory_model], VBE_MODE_DIRECT_COLOR
  jne .loop
  ; Compare bits per pixel to that of the current best mode.
  mov al, [vbe_mode_bpp]
  cmp al, bl
  jb .loop
  ; We require the bits per pixel to be a multiple of 8 not greater than 32 (that is, either 8, 16, 24, or 32).
  ror al, 3
  cmp al, 4
  ja .loop
  ; If we reach this point, we have found the new best mode.
  ; Update all the registers to reflect this fact.
  mov bp, cx
  mov edx, eax
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

; Disk Address Packet
; The segment is set so that the offset is 0.
; This allows loading up to 64 KiB, as the entire load buffer must fit inside a single segment.
; Since the boot sector is already loaded, we start loading from sector 1.
align 4
int13_dap:
  db 0x10 ; size of DAP
  db 0 ; unused
  dw kernel_sector_count - 1 ; number of sectors to read
  dw 0x0000 ; offset of load buffer
  dw 0x07E0 ; segment of load buffer
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
times 510 - ($-$$) db 0
dw 0xAA55

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
  cli
  ; Load GDT
  lgdt [gdtr32]
  ; Set PE in CR0
  mov eax, cr0
  or eax, CR0_PE
  mov cr0, eax
  ; Jump to enter protected mode
  jmp SEGMENT_KERNEL_CODE:protected_mode_start

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
  mov dword [pd_id], 0 | PAGE_GLOBAL | PAGE_LARGE | PAGE_WRITE | PAGE_PRESENT
  ; Map bottom of stack at last page in PML4E number STACK_PML4E
  mov dword [pml4 + STACK_PML4E * 8], pdpt_stack | PAGE_WRITE | PAGE_PRESENT
  mov dword [pdpt_stack + 0x1FF * 8], pd_stack | PAGE_WRITE | PAGE_PRESENT
  mov dword [pd_stack + 0x1FF * 8], pt_stack | PAGE_WRITE | PAGE_PRESENT
  mov dword [pt_stack + 0x1FF * 8], stack | PAGE_GLOBAL | PAGE_WRITE | PAGE_PRESENT
  mov dword [pt_stack + 0x1FF * 8 + 4], PAGE_NX >> 32
  ; Set PML4E number RECURSIVE_PML4E to point at the PML4
  ; This allows access to the page tables through memory.
  mov dword [pml4 + RECURSIVE_PML4E * 8], pml4 | PAGE_WRITE | PAGE_PRESENT
  mov dword [pml4 + RECURSIVE_PML4E * 8 + 4], PAGE_NX >> 32
  ; Set up mapping for framebuffer and page stack
  ; The pd_fb and pdpt_page_stack be filled in by the kernel.
  mov dword [pml4 + FB_PML4E * 8], pdpt_fb | PAGE_WRITE | PAGE_PRESENT
  mov dword [pdpt_fb], pd_fb | PAGE_WRITE | PAGE_PRESENT
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

  ; Enable PAE and PGE in CR4
  mov eax, cr4
  or eax, CR4_PAE | CR4_PGE
  mov cr4, eax
  ; Set CR3 to address of PML4
  mov eax, pml4
  mov cr3, eax
  ; Set SCE, LME, and NXE in EFER MSR
  mov ecx, EFER_MSR
  rdmsr
  or eax, EFER_MSR_SCE | EFER_MSR_LME | EFER_MSR_NXE
  wrmsr
  ; Set PG in CR0
  mov eax, cr0
  or eax, CR0_PG
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
  call kernel_main
  ; Loop forever if kernel exits - this shouldn't happen
.halt:
  cli
  hlt
  jmp .halt