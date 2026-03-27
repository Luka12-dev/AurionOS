/*
 * Interrupt Handlers for Aurion OS
 * PIC remap, timer, CPU exception handler, and legacy disk I/O wrappers
*/

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* PIC ports */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define EOI       0x20

/* Shutdown flag */
static volatile int shutting_down = 0;

void set_shutting_down(void) {
    shutting_down = 1;
}

/* External functions */
extern void c_puts(const char *s);
extern void set_attr(uint8_t a);

/* ATA driver — disk I/O lives in ata.c */
extern int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer);
extern int ata_write_sectors(uint32_t lba, uint8_t count, const void *buffer);

/* Register structure pushed by ISR/IRQ stubs */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} registers_t;

/* System tick counter (incremented by IRQ0 at 100 Hz) */
static volatile uint32_t timer_ticks = 0;

/* I/O Primitives */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %b0, %w1" : : "a"(val), "Nd"(port));
}

/* ~1 microsecond delay via POST diagnostic port
 * Kept static inline - the global io_wait symbol is in io.asm */
static inline void handlers_io_wait(void) {
    outb(0x80, 0);
}

/* PIC Remap */
void pic_remap(void) {
    outb(PIC1_CMD,  0x11); handlers_io_wait();
    outb(PIC2_CMD,  0x11); handlers_io_wait();

    outb(PIC1_DATA, 0x20); handlers_io_wait();
    outb(PIC2_DATA, 0x28); handlers_io_wait();

    outb(PIC1_DATA, 0x04); handlers_io_wait();
    outb(PIC2_DATA, 0x02); handlers_io_wait();

    outb(PIC1_DATA, 0x01); handlers_io_wait();
    outb(PIC2_DATA, 0x01); handlers_io_wait();

    outb(PIC1_DATA, 0xFC); handlers_io_wait();
    outb(PIC2_DATA, 0xFF); handlers_io_wait();
}

/* IRQ0 Timer Handler */
void timer_handler(registers_t *regs) {
    (void)regs;
    timer_ticks++;
}

/* CPU Exception Handler */
void isr_handler(registers_t *regs) {
    (void)regs;

    if (shutting_down) {
        __asm__ volatile("cli");
        for (;;) __asm__ volatile("hlt");
    }

    c_puts("\nCPU EXCEPTION - SYSTEM HALTED\n");
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

/* System Tick Counter */
uint32_t get_ticks(void) {
    return timer_ticks;
}

/* Legacy Disk I/O Wrappers 
 *
 * The rest of the kernel (filesystem, desktop, commands) calls these
 * with a uint32_t sector count. The ATA driver in ata.c takes uint8_t
 * count, so we transfer one sector at a time for safety.
*/

int disk_read_lba(uint32_t lba, uint32_t count, void *buffer) {
    uint8_t *buf = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        if (ata_read_sectors(lba + i, 1, buf + i * 512) != 0)
            return -1;
    }
    return 0;
}

int disk_write_lba(uint32_t lba, uint32_t count, const void *buffer) {
    const uint8_t *buf = (const uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        if (ata_write_sectors(lba + i, 1, buf + i * 512) != 0)
            return -1;
    }
    return 0;
}