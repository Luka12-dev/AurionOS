#ifndef VESA_H
#define VESA_H

#include <stdint.h>
#include <stdbool.h>

// Initialize VESA 1920x1080 mode
int vesa_init_1920x1080(void);

// Get framebuffer address
uint32_t vesa_get_framebuffer(void);

// Check if VESA is enabled
bool vesa_is_enabled(void);

// Get screen dimensions
void vesa_get_dimensions(uint16_t *width, uint16_t *height);

#endif // VESA_H
