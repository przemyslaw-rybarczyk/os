OUTPUT_FORMAT(binary)

KERNEL_LOAD_START = 0x7C00;

SECTIONS {
    . = KERNEL_LOAD_START;
    .boot : {
        *(.boot)
    }
    .text ALIGN(4K) : {
        *(.text)
    }
    .rodata ALIGN(4K) : {
        *(.rodata)
    }
    .data ALIGN(4K) : {
        *(.data)
        *(.bss)
        *(COMMON)
    }
    kernel_sector_count = (. - KERNEL_LOAD_START + 511) / 512;
}
