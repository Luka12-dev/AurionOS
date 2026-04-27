/*
 * Firmware blobs for devices (Wi-Fi ucode, etc.).
 * Load real files from disk when drivers need them - no embedded fake payloads.
 */

#include "../include/firmware.h"
#include <stddef.h>

int firmware_init(void) { return 0; }

const firmware_blob_t *firmware_get(uint32_t id) {
    (void)id;
    return NULL;
}

int firmware_load_to_device(uint32_t fw_id, void *device_mmio_base) {
    (void)fw_id;
    (void)device_mmio_base;
    return -1;
}
