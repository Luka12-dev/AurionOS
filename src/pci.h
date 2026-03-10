#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint32_t bar0;
    uint32_t bar1;
    uint8_t irq;
} pci_device_t;

// PCI functions
uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function);
void pci_get_device_info(uint8_t bus, uint8_t device, uint8_t function, pci_device_t *dev);
bool pci_is_wifi_device(pci_device_t *dev);
int pci_enumerate_devices(pci_device_t *devices, int max_devices);
int pci_find_wifi_devices(pci_device_t *devices, int max_devices);
void pci_enable_device(pci_device_t *dev);
const char* pci_get_device_name(pci_device_t *dev);

#endif // PCI_H
pci_device_t pci_find_device(uint16_t vendor_id, uint16_t device_id);
