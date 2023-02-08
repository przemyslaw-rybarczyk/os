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
    }
    kernel_sector_count = (ALIGN(512) - KERNEL_VMA + KERNEL_LMA - BOOT_LMA) / 512;
    kernel_bss_lma_start = ALIGN(16) - KERNEL_VMA + KERNEL_LMA;
    .bss ALIGN(16) : AT(ADDR(.bss) - KERNEL_VMA + KERNEL_LMA) {
        *(.bss)
        *(COMMON)
    }
    kernel_bss_length_dwords = (ALIGN(4) - ADDR(.bss)) / 4;
    kernel_data_page_end = (ALIGN(4K) - KERNEL_VMA) / 4K;
}
