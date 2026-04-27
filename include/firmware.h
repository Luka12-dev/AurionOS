#ifndef FIRMWARE_H
#define FIRMWARE_H

#include <stdbool.h>
#include <stdint.h>


// Firmware IDs
#define FW_INTEL_IWLINUX 1
#define FW_REALTEK_RTL8188 2
#define FW_ATHEROS_ATH9K 3

// Firmware blob structure
typedef struct {
  uint32_t id;
  const uint8_t *data;
  uint32_t size;
  const char *version;
} firmware_blob_t;

// Firmware loader functions
int firmware_init(void);
const firmware_blob_t *firmware_get(uint32_t id);
int firmware_load_to_device(uint32_t fw_id, void *device_mmio_base);

#endif // FIRMWARE_H
