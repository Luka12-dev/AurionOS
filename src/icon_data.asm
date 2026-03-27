; Simple RLE-compressed 96x96 BGRA pixel data for dock icons.
; Generated at build time by Python - decompressed into BSS at boot.
; Format: [byte: count][byte*4: BGRA pixel]

section .rodata

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

icon_raw_terminal:
    incbin "icons/terminal.rle"

icon_raw_browser:
    incbin "icons/Browser.rle"

icon_raw_notepad:
    incbin "icons/notepad.rle"

icon_raw_calculator:
    incbin "icons/calculator.rle"

icon_raw_files:
    incbin "icons/files.rle"

icon_raw_clock:
    incbin "icons/clock.rle"

icon_raw_paint:
    incbin "icons/paint.rle"

icon_raw_sysinfo:
    incbin "icons/sys-info.rle"

icon_raw_folder:
    incbin "icons/folder.rle"

icon_raw_file:
    incbin "icons/file_on_desktop.rle"

icon_raw_settings:
    incbin "icons/settings.rle"

icon_raw_snake:
    incbin "icons/snake.rle"
