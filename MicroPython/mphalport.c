/* AurionOS HAL implementation for MicroPython */

#include "py/mpconfig.h"
#include "py/mphal.h"

/* AurionOS system calls */
extern void c_putc(char c);
extern void c_puts(const char *s);
extern uint16_t c_getkey(void);

/* Receive single character from keyboard */
int mp_hal_stdin_rx_chr(void) {
    uint16_t k = c_getkey();
    return (int)(k & 0xFF);
}

/* Send string of given length to console */
mp_uint_t mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    for (mp_uint_t i = 0; i < len; i++) {
        c_putc(str[i]);
    }
    return len;
}
