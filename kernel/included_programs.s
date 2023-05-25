section .rodata

global included_file_window
global included_file_window_end

; This file includes loadable programs as read-only data

included_file_window:
incbin "build/window/window.bin"
included_file_window_end:
