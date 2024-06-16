section .rodata

global included_file_terminal
global included_file_terminal_end
global included_file_test_program
global included_file_test_program_end

; This file includes loadable programs as read-only data

included_file_terminal:
incbin "build/terminal/terminal.bin"
included_file_terminal_end:

included_file_test_program:
incbin "build/test_program/test_program.bin"
included_file_test_program_end:
