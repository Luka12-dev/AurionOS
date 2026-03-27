#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Error Codes */
#define E_OK                0   /* Success */
#define E_INVAL             1   /* Invalid parameter */
#define E_NOENT             2   /* No such file or directory */
#define E_ACCESS            3   /* Access denied */
#define E_NOMEM             4   /* Out of memory */
#define E_NOSPC             5   /* No space left on device */
#define E_EXIST             6   /* File exists */
#define E_NOTDIR            7   /* Not a directory */
#define E_ISDIR             8   /* Is a directory */
#define E_BADF              9   /* Bad file descriptor */
#define E_IO                10  /* I/O error */
#define E_BUSY              11  /* Device busy */
#define E_AGAIN             12  /* Try again */
#define E_NOTSUPP           13  /* Operation not supported */
#define E_PERM              14  /* Operation not permitted */

/* System Call Numbers */
#define SYS_PRINT_STRING    0x01
#define SYS_PRINT_CHAR      0x02
#define SYS_READ_CHAR       0x04
#define SYS_CLEAR_SCREEN    0x05
#define SYS_SET_COLOR       0x08
#define SYS_CHDIR           0x1A
#define SYS_GETCWD          0x1B
#define SYS_OPENDIR         0x1D
#define SYS_READDIR         0x1E
#define SYS_CLOSEDIR        0x1F
#define SYS_GETPID          0x44
#define SYS_GET_TIME        0x50
#define SYS_GET_DATE        0x51
#define SYS_GET_TICKS       0x52
#define SYS_SYSINFO         0x54
#define SYS_UNAME           0x55
#define SYS_READ_SECTOR     0x61
#define SYS_SHUTDOWN        0x71
#define SYS_BEEP            0x72
#define SYS_DEBUG           0x73

/* External Kernel Functions */
extern void puts(const char* s);
extern void putc(char c);
extern void cls(void);
extern void io_set_attr(uint8_t a);
extern int getkey_block(void);
extern uint32_t get_ticks(void);
extern int disk_read_lba(uint32_t lba, uint32_t count, void* buffer);
extern void set_shutting_down(void);

/* CMOS / RTC Helpers */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) );
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ( "outw %w0, %w1" : : "a"(val), "Nd"(port) );
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ( "inb %w1, %b0" : "=a"(ret) : "Nd"(port) );
    return ret;
}

static uint8_t get_cmos_reg(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

/* System call dispatcher invoked from interrupt.asm */
int syscall_handler(int num, int arg1, int arg2, int arg3) {
    (void)arg2; 
    (void)arg3;

    switch (num) {
        case SYS_PRINT_STRING:
            if ((const char*)arg1) puts((const char*)arg1);
            return E_OK;

        case SYS_PRINT_CHAR:
            putc((char)(arg1 & 0xFF));
            return E_OK;

        case SYS_READ_CHAR:
            return getkey_block();

        case SYS_CLEAR_SCREEN:
            cls();
            return E_OK;

        case SYS_SET_COLOR:
            io_set_attr((uint8_t)(arg1 & 0xFF));
            return E_OK;

        case SYS_CHDIR:
        case SYS_GETCWD:
            return E_OK; 

        case SYS_OPENDIR:
            return -1; /* Simulated FS not mounted yet */

        case SYS_READDIR:
            return -1;

        case SYS_CLOSEDIR:
            return E_OK;

        case SYS_GETPID:
            return 1;

        case SYS_GET_TIME: {
            uint8_t h = get_cmos_reg(0x04);
            uint8_t m = get_cmos_reg(0x02);
            uint8_t s = get_cmos_reg(0x00);
            return (h << 16) | (m << 8) | s;
        }

        case SYS_GET_DATE: {
            uint8_t d = get_cmos_reg(0x07);
            uint8_t m = get_cmos_reg(0x08);
            uint8_t y = get_cmos_reg(0x09);
            return (d << 16) | (m << 8) | y;
        }

        case SYS_GET_TICKS:
            return (int)get_ticks();

        case SYS_READ_SECTOR:
            return disk_read_lba(arg1, arg2, (void*)arg3);

        case SYS_DEBUG:
            puts("[DEBUG] ");
            if ((const char*)arg1) puts((const char*)arg1);
            puts("\n");
            return E_OK;

        case SYS_SHUTDOWN: {
            puts("System shutting down...\n");

            /* Mark system as shutting down so ISR does not panic on stray
               exceptions during the power-off sequence. */
            set_shutting_down();

            /* Give the frontend time to flush the shutdown message. */
            for (volatile int i = 0; i < 2000000; i++) {
                __asm__ volatile("nop");
            }

            /* Disable all maskable interrupts - we do not want any IRQ
               firing while we poke shutdown ports. */
            __asm__ volatile("cli");

            /* Emulator / VM specific ports
               If the hypervisor recognises one of these, execution halts
               immediately and the VM window closes. */

            /* Bochs and old QEMU (pre-2.0) */
            outw(0xB004, 0x2000);

            /* QEMU 2.0+ (PIIX4 PM) */
            outw(0x604, 0x2000);

            /* VirtualBox (per OSDev wiki) */
            outw(0x4004, 0x3400);

            /* Small delay - if we are still running, none of the above
               worked so this is real hardware (or an unusual VM). */
            for (volatile int i = 0; i < 500000; i++) {
                __asm__ volatile("nop");
            }

            /* ACPI S5 shutdown for real hardware
               Walk RSDP -> RSDT -> FADT -> DSDT to find PM1a_CNT and the
               SLP_TYP value required for S5 (soft-off). */

            /* Scan for the RSDP signature "RSD PTR " in the BIOS area. */
            uint8_t *rsdp_ptr = NULL;

            /* EBDA pointer is at physical 0x040E (segment) */
            uint16_t ebda_seg = *(uint16_t *)0x040E;
            uint32_t ebda_addr = (uint32_t)ebda_seg << 4;

            /* Search first 1 KB of EBDA */
            for (uint32_t addr = ebda_addr; addr < ebda_addr + 1024; addr += 16) {
                uint8_t *p = (uint8_t *)addr;
                if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' && p[3] == ' ' &&
                    p[4] == 'P' && p[5] == 'T' && p[6] == 'R' && p[7] == ' ') {
                    /* Validate checksum (first 20 bytes must sum to 0 mod 256) */
                    uint8_t sum = 0;
                    for (int j = 0; j < 20; j++) sum += p[j];
                    if ((sum & 0xFF) == 0) { rsdp_ptr = p; break; }
                }
            }

            /* Search main BIOS area 0xE0000 - 0xFFFFF */
            if (!rsdp_ptr) {
                for (uint32_t addr = 0x000E0000; addr < 0x00100000; addr += 16) {
                    uint8_t *p = (uint8_t *)addr;
                    if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' && p[3] == ' ' &&
                        p[4] == 'P' && p[5] == 'T' && p[6] == 'R' && p[7] == ' ') {
                        uint8_t sum = 0;
                        for (int j = 0; j < 20; j++) sum += p[j];
                        if ((sum & 0xFF) == 0) { rsdp_ptr = p; break; }
                    }
                }
            }

            if (rsdp_ptr) {
                /* RSDP found. Byte 16..19 is the RSDT physical address. */
                uint32_t rsdt_addr = *(uint32_t *)(rsdp_ptr + 16);
                uint8_t *rsdt = (uint8_t *)rsdt_addr;

                /* Verify RSDT signature "RSDT" */
                if (rsdt[0] == 'R' && rsdt[1] == 'S' && rsdt[2] == 'D' && rsdt[3] == 'T') {
                    uint32_t rsdt_len = *(uint32_t *)(rsdt + 4);
                    uint32_t entry_count = (rsdt_len - 36) / 4;

                    /* Walk RSDT entries looking for FADT (signature "FACP") */
                    uint8_t *fadt = NULL;
                    for (uint32_t i = 0; i < entry_count; i++) {
                        uint32_t entry_addr = *(uint32_t *)(rsdt + 36 + i * 4);
                        uint8_t *hdr = (uint8_t *)entry_addr;
                        if (hdr[0] == 'F' && hdr[1] == 'A' && hdr[2] == 'C' && hdr[3] == 'P') {
                            fadt = hdr;
                            break;
                        }
                    }

                    if (fadt) {
                        /* FADT offsets (ACPI spec):
                           40: DSDT address (4 bytes)
                           64: PM1a_CNT_BLK (4 bytes)
                           68: PM1b_CNT_BLK (4 bytes) */
                        uint32_t dsdt_addr = *(uint32_t *)(fadt + 40);
                        uint16_t pm1a_cnt = (uint16_t)(*(uint32_t *)(fadt + 64));
                        uint16_t pm1b_cnt = (uint16_t)(*(uint32_t *)(fadt + 68));

                        /* Parse the DSDT to find \_S5 object.
                           We look for the byte sequence 0x08 '_' 'S' '5' '_'
                           which is the AML NameOp for \_S5. */
                        uint8_t *dsdt = (uint8_t *)dsdt_addr;
                        uint32_t dsdt_len = *(uint32_t *)(dsdt + 4);

                        uint16_t slp_typa = 0;
                        uint16_t slp_typb = 0;
                        bool s5_found = false;

                        for (uint32_t i = 36; i < dsdt_len - 8; i++) {
                            /* Look for NameOp (0x08) followed by _S5_ */
                            if (dsdt[i] == 0x08 &&
                                dsdt[i+1] == '_' && dsdt[i+2] == 'S' &&
                                dsdt[i+3] == '5' && dsdt[i+4] == '_') {
                                /* Found \_S5. Skip the name (5 bytes) then
                                   parse the package. The next byte should be
                                   0x12 (PackageOp). */
                                uint32_t j = i + 5;

                                /* Skip possible leading byte (could be 0x12 directly
                                   or prefixed with backslash) */
                                if (dsdt[j] == 0x12) {
                                    j++; /* PackageOp */
                                    /* PkgLength - can be 1-4 bytes. Check high bits. */
                                    uint8_t pkg_lead = dsdt[j];
                                    uint8_t pkg_len_bytes = (pkg_lead >> 6) & 3;
                                    j += 1 + pkg_len_bytes; /* skip PkgLength */
                                    j++; /* NumElements */

                                    /* Now read SLP_TYPa. It might be:
                                       - 0x0A xx (BytePrefix + byte value)
                                       - 0x0B xxxx (WordPrefix + word value)
                                       - A raw byte 0x00..0x3F (if small enough) */
                                    if (dsdt[j] == 0x0A) {
                                        slp_typa = dsdt[j + 1];
                                        j += 2;
                                    } else if (dsdt[j] == 0x0B) {
                                        slp_typa = *(uint16_t *)(dsdt + j + 1);
                                        j += 3;
                                    } else {
                                        slp_typa = dsdt[j];
                                        j += 1;
                                    }

                                    /* SLP_TYPb */
                                    if (dsdt[j] == 0x0A) {
                                        slp_typb = dsdt[j + 1];
                                    } else if (dsdt[j] == 0x0B) {
                                        slp_typb = *(uint16_t *)(dsdt + j + 1);
                                    } else {
                                        slp_typb = dsdt[j];
                                    }

                                    s5_found = true;
                                    break;
                                }
                            }
                        }

                        if (s5_found && pm1a_cnt != 0) {
                            /* SLP_EN is bit 13 = 0x2000
                               SLP_TYP goes into bits 10-12 */
                            uint16_t val_a = (slp_typa << 10) | 0x2000;
                            outw(pm1a_cnt, val_a);

                            if (pm1b_cnt != 0) {
                                uint16_t val_b = (slp_typb << 10) | 0x2000;
                                outw(pm1b_cnt, val_b);
                            }

                            /* If ACPI worked, we should be powered off by now.
                               Wait a moment just in case. */
                            for (volatile int i = 0; i < 5000000; i++) {
                                __asm__ volatile("nop");
                            }
                        }
                    }
                }
            }

            /* ---- Last resort: halt the CPU ----
               If nothing worked, just halt. Better than triple-faulting. */
            puts("Power off failed. System halted.\n");
            for (;;) {
                __asm__ volatile("hlt");
            }

            /* Never reached, but satisfy the compiler. */
            return E_NOTSUPP;
        }
        
        case SYS_BEEP: {
            /* PC Speaker beep using PIT and port 0x61 */
            uint32_t freq = (uint32_t)arg1;
            uint32_t duration = (uint32_t)arg2;
            
            if (freq < 20 || freq > 20000) return E_INVAL;
            
            uint32_t divisor = 1193180 / freq;
            
            /* Set PIT channel 2 to square wave mode */
            outb(0x43, 0xB6);
            outb(0x42, (uint8_t)(divisor & 0xFF));
            outb(0x42, (uint8_t)((divisor >> 8) & 0xFF));
            
            /* Enable speaker */
            uint8_t tmp = inb(0x61);
            outb(0x61, tmp | 0x03);
            
            /* Simple delay loop for duration */
            for (uint32_t i = 0; i < duration * 1000; i++) {
                __asm__ volatile("nop");
            }
            
            /* Disable speaker */
            outb(0x61, tmp & 0xFC);
            
            return E_OK;
        }
        
        default:
            return -1;
    }
}

/* Internal Syscall Wrappers */

static inline int32_t syscall0(uint32_t num) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num) : "memory");
    return ret;
}

static inline int32_t syscall1(uint32_t num, uint32_t arg1) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline int32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2) : "memory");
    return ret;
}

static inline int32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    int32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory" );
    return ret;
}

/* Public API Functions (Linked by commands.c) */

/* BCD to Binary helper */
static inline uint8_t bcd2bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

int sys_get_time(uint8_t* hours, uint8_t* minutes, uint8_t* seconds) {
    // Direct CMOS read to avoid syscall issues
    *hours = bcd2bin(get_cmos_reg(0x04));
    *minutes = bcd2bin(get_cmos_reg(0x02));
    *seconds = bcd2bin(get_cmos_reg(0x00));
    return E_OK;
}

int sys_get_date(uint8_t* day, uint8_t* month, uint16_t* year) {
    // Direct CMOS read to avoid syscall issues
    *day = bcd2bin(get_cmos_reg(0x07));
    *month = bcd2bin(get_cmos_reg(0x08));
    *year = 2000 + bcd2bin(get_cmos_reg(0x09));
    return E_OK;
}

int sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

int sys_read_sector(uint32_t lba, uint32_t count, void* buffer) {
    return syscall3(SYS_READ_SECTOR, lba, count, (uint32_t)buffer);
}

int sys_print(const char* str) {
    return syscall1(SYS_PRINT_STRING, (uint32_t)str);
}

int sys_putc(char c) {
    return syscall1(SYS_PRINT_CHAR, (uint32_t)c);
}

int sys_clear_screen(void) {
    return syscall0(SYS_CLEAR_SCREEN);
}

int sys_set_color(uint8_t color) {
    return syscall1(SYS_SET_COLOR, color);
}

int sys_chdir(const char* path) {
    return syscall1(SYS_CHDIR, (uint32_t)path);
}

int sys_getcwd(char* buffer, uint32_t size) {
    return syscall2(SYS_GETCWD, (uint32_t)buffer, size);
}

int sys_opendir(const char* path) {
    return syscall1(SYS_OPENDIR, (uint32_t)path);
}

int sys_readdir(int fd, void* entry) {
    return syscall2(SYS_READDIR, (uint32_t)fd, (uint32_t)entry);
}

int sys_closedir(int fd) {
    return syscall1(SYS_CLOSEDIR, (uint32_t)fd);
}

int sys_sysinfo(void* info) {
    return syscall1(SYS_SYSINFO, (uint32_t)info);
}

int sys_uname(char* buffer, uint32_t size) {
    return syscall2(SYS_UNAME, (uint32_t)buffer, size);
}

int sys_debug(const char* message) {
    if (!message) return -E_INVAL;
    return syscall1(SYS_DEBUG, (uint32_t)message);
}

const char* sys_strerror(int error) {
    switch (error) {
        case E_OK:      return "Success";
        case E_INVAL:   return "Invalid parameter";
        case E_NOENT:   return "No such file or directory";
        case E_ACCESS:  return "Access denied";
        case E_NOMEM:   return "Out of memory";
        case E_NOSPC:   return "No space left on device";
        case E_EXIST:   return "File exists";
        case E_NOTDIR:  return "Not a directory";
        case E_ISDIR:   return "Is a directory";
        case E_BADF:    return "Bad file descriptor";
        case E_IO:      return "I/O error";
        case E_BUSY:    return "Device busy";
        case E_AGAIN:   return "Try again";
        case E_NOTSUPP: return "Operation not supported";
        case E_PERM:    return "Operation not permitted";
        default:        return "Unknown error";
    }
}

int sys_errno(int result) {
    return (result < 0) ? -result : E_OK;
}

void sys_shutdown(void) {
    syscall0(SYS_SHUTDOWN);
    /* If the syscall somehow returned, halt forever */
    __asm__ volatile("cli");
    for (;;) { __asm__ volatile("hlt"); }
}

int sys_beep(uint32_t frequency, uint32_t duration) {
    return syscall2(SYS_BEEP, frequency, duration);
}

void sys_perror(const char* prefix) {
    if (prefix) {
        puts(prefix);
        puts(": ");
    }
}

// Read file from host (simulated by reading from a specific disk sector)
// This allows AurionOS to read files prepared by the host system
int read_file_from_host(const char *filename, void *buffer, uint32_t max_size) {
    // For now, we'll use a simple approach: read from a reserved disk sector
    // The wifi_networks.dat file should be placed at sector 1000
    // In a real implementation, this could use a shared memory area or disk file
    
    extern int disk_read_lba(uint32_t lba, uint32_t count, void* buffer);
    
    (void)filename; // Not used yet - we know it's wifi_networks.dat
    
    if (buffer == NULL || max_size == 0) {
        return -1;
    }
    
    // Read from sector 1000 (reserved for host communication)
    uint8_t sector_buffer[512];
    if (disk_read_lba(1000, 1, sector_buffer) != 0) {
        return -1;
    }
    
    // Copy to output buffer
    uint32_t bytes_to_copy = (max_size < 512) ? max_size : 512;
    for (uint32_t i = 0; i < bytes_to_copy; i++) {
        ((uint8_t*)buffer)[i] = sector_buffer[i];
    }
    
    return (int)bytes_to_copy;
}
