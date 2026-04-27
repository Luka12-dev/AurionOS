/*
 * AurionOS Port I/O Header
 * Shared inline functions for hardware port access
*/

#ifndef _RODOS_PORTIO_H
#define _RODOS_PORTIO_H

#include <stdint.h>

/* Prevent multiple definitions by using inline */
static inline void io_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t io_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t io_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t io_inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Block I/O operations */
static inline void io_insw(uint16_t port, void *addr, uint32_t count) {
    __asm__ volatile ("rep insw" 
                      : "+D"(addr), "+c"(count)
                      : "d"(port)
                      : "memory");
}

static inline void io_outsw(uint16_t port, const void *addr, uint32_t count) {
    __asm__ volatile ("rep outsw"
                      : "+S"(addr), "+c"(count)
                      : "d"(port)
                      : "memory");
}

/* Convenience macros for backward compatibility */
#ifndef outb
#define outb io_outb
#endif
#ifndef inb
#define inb io_inb
#endif
#ifndef outw
#define outw io_outw
#endif
#ifndef inw
#define inw io_inw
#endif
#ifndef outl
#define outl io_outl
#endif
#ifndef inl
#define inl io_inl
#endif
#ifndef insw
#define insw io_insw
#endif
#ifndef outsw
#define outsw io_outsw
#endif

#endif /* _RODOS_PORTIO_H */
