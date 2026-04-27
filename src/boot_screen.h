/*
 * Boot Screen for AurionOS
 * Shows logo and loading animation
 */

#ifndef BOOT_SCREEN_H
#define BOOT_SCREEN_H

#include <stdint.h>
#include <stdbool.h>

/* Show boot screen with logo and spinner, returns when complete */
void boot_screen_show(void);

#endif /* BOOT_SCREEN_H */
