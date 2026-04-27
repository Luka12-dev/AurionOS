; icon_data.asm - Empty stub (icons now loaded from BMP files at runtime)
;
; Icons are now loaded dynamically from /icons/*.bmp files
; This file is kept for build compatibility but contains no data

section .rodata

; Empty stubs for backward compatibility
global icon_raw_terminal
global icon_raw_browser
global icon_raw_notepad
global icon_raw_calculator
global icon_raw_files
global icon_raw_clock
global icon_raw_paint
global icon_raw_sysinfo
global icon_raw_folder
global icon_raw_file
global icon_raw_settings
global icon_raw_snake
global icon_raw_3d_demo

icon_raw_terminal:
    db 0
icon_raw_browser:
    db 0
icon_raw_notepad:
    db 0
icon_raw_calculator:
    db 0
icon_raw_files:
    db 0
icon_raw_clock:
    db 0
icon_raw_paint:
    db 0
icon_raw_sysinfo:
    db 0
icon_raw_folder:
    db 0
icon_raw_file:
    db 0
icon_raw_settings:
    db 0
icon_raw_snake:
    db 0
icon_raw_3d_demo:
    db 0
