section .rodata

global included_file_fat32
global included_file_fat32_end

; This file includes loadable programs as read-only data

included_file_fat32:
incbin "build/fat32/fat32.bin"
included_file_fat32_end:
