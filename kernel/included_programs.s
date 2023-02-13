section .rodata

global included_file_program
global included_file_program_end

; This file includes loadable programs as read-only data

included_file_program:
incbin "build/program/program.bin"
included_file_program_end:
