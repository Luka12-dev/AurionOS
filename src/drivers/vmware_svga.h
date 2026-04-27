/*
 * VMware SVGA II Driver Header
*/

#ifndef VMWARE_SVGA_H
#define VMWARE_SVGA_H

#include <stdint.h>
#include <stdbool.h>

/* Detect VMware SVGA hardware */
bool vmware_svga_detect(void);

/* Check if VMware SVGA is available */
bool vmware_svga_available(void);

/* Disable VMware SVGA (for text mode) */
void vmware_svga_disable(void);

/* Set video mode */
bool vmware_svga_set_mode(uint32_t width, uint32_t height, uint32_t bpp);

/* Get maximum supported resolution */
void vmware_svga_get_max_resolution(uint32_t *width, uint32_t *height);

/* Get framebuffer address */
uint32_t vmware_svga_get_fb_addr(void);

/* Get framebuffer size */
uint32_t vmware_svga_get_fb_size(void);

#endif
