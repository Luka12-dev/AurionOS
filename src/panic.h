/*
 * AurionOS Kernel Panic Handler
 */

#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

/* Kernel panic with full details */
void kernel_panic(const char *message, const char *file, int line, uint32_t error_code);

/* Simple panic */
void panic(const char *message);

/* Panic with error code */
void panic_code(const char *message, uint32_t code);

/* Convenience macro for panic with file/line */
#define PANIC(msg) kernel_panic(msg, __FILE__, __LINE__, 0)
#define PANIC_CODE(msg, code) kernel_panic(msg, __FILE__, __LINE__, code)

#endif
