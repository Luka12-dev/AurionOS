; Pre-decoded 48x48 BGRA raw pixel data for dock icons.
; Generated at build time by Python - zero runtime decoding needed.
; Each file is exactly 48*48*4 = 9216 bytes of raw BGRA pixels.

section .rodata

global icon_raw_terminal
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

icon_raw_terminal:
    incbin "icons/terminal_48.raw"

icon_raw_notepad:
    incbin "icons/notepad_48.raw"

icon_raw_calculator:
    incbin "icons/calculator_48.raw"

icon_raw_files:
    incbin "icons/files_48.raw"

icon_raw_clock:
    incbin "icons/clock_48.raw"

icon_raw_paint:
    incbin "icons/paint_48.raw"

icon_raw_sysinfo:
    incbin "icons/sys-info_48.raw"

icon_raw_folder:
    incbin "icons/folder_48.raw"

icon_raw_file:
    incbin "icons/file_on_desktop_48.raw"

icon_raw_settings:
    incbin "icons/settings_48.raw"

icon_raw_snake:
    incbin "icons/snake_48.raw"
