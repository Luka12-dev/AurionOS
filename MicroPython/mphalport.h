#ifndef MICROPY_INCLUDED_AURIONOS_MPHALPORT_H
#define MICROPY_INCLUDED_AURIONOS_MPHALPORT_H

#include "py/mpconfig.h"

/* AurionOS HAL functions */
extern void c_putc(char c);
extern uint16_t c_getkey(void);

/* Timing - AurionOS doesn't have a timer yet, return 0 */
static inline mp_uint_t mp_hal_ticks_ms(void) {
    return 0;
}

static inline mp_uint_t mp_hal_ticks_us(void) {
    return 0;
}

static inline void mp_hal_delay_ms(mp_uint_t ms) {
    /* Busy wait - not ideal but works for now */
    for (volatile uint32_t i = 0; i < ms * 10000; i++) {
        __asm__ volatile("nop");
    }
}

/* Interrupt character handling */
static inline void mp_hal_set_interrupt_char(char c) {
    /* Not implemented yet */
    (void)c;
}

#endif
