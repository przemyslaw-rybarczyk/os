section .rodata

global included_file_init
global included_file_init_end

; This file includes loadable programs as read-only data

included_file_init:
incbin "build/init/init.bin"
included_file_init_end:
