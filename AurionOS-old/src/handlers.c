#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* PIC ports and constants */
#define PIC1_CMD 0x20
#define PIC1_DATA 0x21
#define PIC2_CMD 0xA0
#define PIC2_DATA 0xA1
#define EOI 0x20

/* ATA PIO Ports */
#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

/* Shutdown flag */
static volatile int shutting_down = 0;

void set_shutting_down(void)
{
    shutting_down = 1;
}

/* External kernel/IO functions */
extern void c_puts(const char *s);
extern void set_attr(uint8_t a);

/* Register structure */
typedef struct
{
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} registers_t;

/* Global system state */
static volatile uint32_t timer_ticks = 0;

/* I/O primitives */
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "dN"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    /* Using %b0 forces the compiler to use AL (the byte register) */
    __asm__ volatile("outb %b0, %w1" : : "a"(val), "Nd"(port));
}

static inline void insw(uint16_t port, void *addr, uint32_t count)
{
    __asm__ volatile(
        "cld; rep insw"
        : "+D"(addr), "+c"(count)
        : "d"(port)
        : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t count)
{
    __asm__ volatile(
        "cld; rep outsw"
        : "+S"(addr), "+c"(count)
        : "d"(port)
        : "memory");
}

/*
 PIC remap
*/
static inline void io_wait(void)
{
    /* Port 0x80 is used for 'checkpoints' during POST. */
    /* Doing an IO write to this port should take ~1 microsecond */
    outb(0x80, 0);
}

void pic_remap(void)
{
    /* ICW1 - start initialization */
    outb(PIC1_CMD, 0x11);
    io_wait();
    outb(PIC2_CMD, 0x11);
    io_wait();

    /* ICW2 - set vector offsets */
    outb(PIC1_DATA, 0x20);
    io_wait();
    outb(PIC2_DATA, 0x28);
    io_wait();

    /* ICW3 - setup cascading */
    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    /* ICW4 - 8086/88 mode */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* Masks: IRQ0 (timer) and IRQ1 (keyboard) enabled on Master */
    outb(PIC1_DATA, 0xFC);
    io_wait();
    outb(PIC2_DATA, 0xFF);
    io_wait();
}

void timer_handler(registers_t *regs)
{
    (void)regs;
    timer_ticks++;
}

void isr_handler(registers_t *regs)
{
    if (shutting_down)
    {
        /* During shutdown, just halt - don't print error */
        __asm__ volatile("cli");
        for (;;)
        {
            __asm__ volatile("hlt");
        }
    }

    (void)regs;
    c_puts("\nCPU EXCEPTION - SYSTEM HALTED\n");
    __asm__ volatile("cli");
    for (;;)
    {
        __asm__ volatile("hlt");
    }
}

/* Utility */
uint32_t get_ticks(void)
{
    return timer_ticks;
}

int disk_read_lba(uint32_t lba, uint32_t count, void *buffer)
{
    uint8_t status;
    uint16_t *buf_ptr = (uint16_t *)buffer;

    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, (uint8_t)count);
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, 0x20);

    for (int k = 0; k < 4; ++k)
    {
        (void)inb(ATA_STATUS);
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        /* Wait with timeout */
        int timeout = 100000;
        while (timeout-- > 0)
        {
            status = inb(ATA_STATUS);
            if (status & ATA_SR_ERR)
                return -1;
            if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
                break;
        }
        if (timeout <= 0)
            return -1; /* Timeout */

        insw(ATA_DATA, buf_ptr, 256);
        buf_ptr += 256;
    }
    return 0;
}
/* Disk write function for persistence - FIXED VERSION */
int disk_write_lba(uint32_t lba, uint32_t count, void *buffer)
{
    uint8_t status;
    uint16_t *buf_ptr = (uint16_t *)buffer;

    /* Select drive and set LBA mode */
    outb(ATA_DRIVE_HEAD, (uint8_t)(0xE0 | ((lba >> 24) & 0x0F)));

    /* Wait for drive to be ready */
    int timeout = 1000000;
    while (timeout-- > 0)
    {
        status = inb(ATA_STATUS);
        if (!(status & ATA_SR_BSY))
            break;
    }
    if (timeout <= 0)
        return -1;

    /* Send write command */
    outb(ATA_SECTOR_COUNT, (uint8_t)count);
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, 0x30); /* WRITE SECTORS command */

    /* Small delay */
    for (int k = 0; k < 4; ++k)
    {
        (void)inb(ATA_STATUS);
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        /* Wait for drive to be ready to receive data */
        timeout = 1000000;
        while (timeout-- > 0)
        {
            status = inb(ATA_STATUS);
            if (status & ATA_SR_ERR)
                return -1;
            if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
                break;
        }
        if (timeout <= 0)
            return -1;

        /* Write 256 words (512 bytes) using outsw */
        outsw(ATA_DATA, buf_ptr, 256);
        buf_ptr += 256;

        /* Wait for write to complete */
        timeout = 1000000;
        while (timeout-- > 0)
        {
            status = inb(ATA_STATUS);
            if (!(status & ATA_SR_BSY))
                break;
        }
        if (timeout <= 0)
            return -1;
    }

    /* Flush cache */
    outb(ATA_COMMAND, 0xE7); /* FLUSH CACHE command */
    timeout = 1000000;
    while (timeout-- > 0)
    {
        status = inb(ATA_STATUS);
        if (!(status & ATA_SR_BSY))
            break;
    }

    return 0;
}