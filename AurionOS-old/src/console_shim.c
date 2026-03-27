/*
 * Console Shim for Aurion OS
 * Redirects c_puts/c_putc to either VGA text mode or Terminal window
*/

#include <stdint.h>
#include <stdbool.h>

/* Original VGA functions from io.asm (renamed) */
extern void vga_putc(char c);
extern void vga_puts(const char *s);
extern void vga_cls(void);

/* Hook for terminal redirection */
static void (*terminal_putc_hook)(char) = 0;

void set_terminal_hook(void (*hook)(char))
{
    terminal_putc_hook = hook;
}

extern void serial_putc(char c);

/* Public API - used by kernel, shell, printf, etc. */
void c_putc(char c)
{
    /* Skip serial output for now - may cause hangs */
    /* serial_putc(c); */

    if (terminal_putc_hook)
    {
        terminal_putc_hook(c);
    }
    else
    {
        vga_putc(c);
    }
}

void c_puts(const char *s)
{
    while (*s)
    {
        c_putc(*s++);
    }
}

void c_cls(void)
{
    if (terminal_putc_hook)
    {
        /* Terminal handles clear screen via command, not this low-level call usually */
        /* But we could send a clear code if needed */
    }
    else
    {
        vga_cls();
    }
}

/* Aliases for some code calling puts/putc directly */
void putc(char c) { c_putc(c); }
void puts(const char *s) { c_puts(s); }
void cls(void) { c_cls(); }
