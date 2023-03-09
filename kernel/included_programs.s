section .rodata

global included_file_program1
global included_file_program1_end
global included_file_program2
global included_file_program2_end

; This file includes loadable programs as read-only data

included_file_program1:
incbin "build/program1/program1.bin"
included_file_program1_end:

included_file_program2:
incbin "build/program2/program2.bin"
included_file_program2_end:
