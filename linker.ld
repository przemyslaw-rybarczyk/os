OUTPUT_FORMAT(binary)

BOOT_LMA = 0x7C00;
KERNEL_VMA = 0xFFFFFFFFC0000000;

SECTIONS {
    . = BOOT_LMA;
    .boot : {
        *(.boot)
    }
    KERNEL_LMA = ALIGN(4K);
    . = KERNEL_VMA;
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA + KERNEL_LMA) {
        *(.text)
    }
    kernel_text_page_end = (ALIGN(4K) - KERNEL_VMA) / 4K;
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA + KERNEL_LMA) {
        *(.rodata)
    }
    kernel_rodata_page_end = (ALIGN(4K) - KERNEL_VMA) / 4K;
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA + KERNEL_LMA) {
        *(.data)
        *(.bss)
        *(COMMON)
    }
    kernel_data_page_end = (ALIGN(4K) - KERNEL_VMA) / 4K;
    kernel_sector_count = (. - KERNEL_VMA + KERNEL_LMA - BOOT_LMA + 511) / 512;
}
