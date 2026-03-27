/*
 * link.ld - AurionOS Linker Script
 * Purpose: Define memory layout for 32-bit protected mode kernel
*/

OUTPUT_FORMAT("binary")
OUTPUT_ARCH(i386)

/* Entry point */
ENTRY(kernel_entry)

/* Memory regions */
SECTIONS
{
    /* Kernel is loaded at 0x10000 (64KB) by bootloader */
    . = 0x10000;

    .text : ALIGN(16)
    {
        /* Ensure the kernel entry point appears first in the binary */
        *(.text.start)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(16)
    {
        *(.data)
        *(.data.*)
    }

    /* Place uninitialized BSS section at 2MB to avoid overlapping VGA memory & BIOS ROMs.
       The kernel code (.text + .data) fits in low memory well below 640KB limit. */
    . = 0x200000;
    .bss : ALIGN(16)
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Define kernel end and heap start */
    . = ALIGN(16);
    __kernel_end = .;
    __heap_start = .;
    
    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}