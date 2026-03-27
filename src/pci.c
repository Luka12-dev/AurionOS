/*
 * PCI Bus Driver for AurionOS
 * Implements PCI device enumeration and configuration
*/

#include "../include/stddef.h"
#include <stdbool.h>
#include <stdint.h>


// PCI Configuration Space Access
#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA 0x0CFC

// PCI Device Classes
#define PCI_CLASS_NETWORK 0x02
#define PCI_SUBCLASS_WIFI 0x80

// PCI Vendor IDs
#define PCI_VENDOR_INTEL 0x8086
#define PCI_VENDOR_REALTEK 0x10EC
#define PCI_VENDOR_ATHEROS 0x168C
#define PCI_VENDOR_BROADCOM 0x14E4
#define PCI_VENDOR_RALINK 0x1814

// Common WiFi Device IDs
// Intel WiFi
#define PCI_DEVICE_INTEL_AC7260 0x08B1
#define PCI_DEVICE_INTEL_AC8260 0x24F3
#define PCI_DEVICE_INTEL_AC9260 0x2526
#define PCI_DEVICE_INTEL_AX200 0x2723
#define PCI_DEVICE_INTEL_AX201 0x43F0

// Realtek WiFi
#define PCI_DEVICE_RTL8188EE 0x8179
#define PCI_DEVICE_RTL8192EE 0x818B
#define PCI_DEVICE_RTL8821AE 0x8821
#define PCI_DEVICE_RTL8822BE 0xB822

// Atheros WiFi
#define PCI_DEVICE_ATH9K 0x0029
#define PCI_DEVICE_ATH10K 0x003C

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

// I/O port operations
static inline void outl(uint16_t port, uint32_t val) {
  __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
  uint32_t ret;
  __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
  __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
  uint16_t ret;
  __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
  __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
  uint8_t ret;
  __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

// Read from PCI configuration space
uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t function,
                         uint8_t offset) {
  uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) |
                                (offset & 0xFC) | 0x80000000);

  outl(PCI_CONFIG_ADDRESS, address);
  return inl(PCI_CONFIG_DATA);
}

// Write to PCI configuration space
void pci_config_write(uint8_t bus, uint8_t device, uint8_t function,
                      uint8_t offset, uint32_t value) {
  uint32_t address = (uint32_t)((bus << 16) | (device << 11) | (function << 8) |
                                (offset & 0xFC) | 0x80000000);

  outl(PCI_CONFIG_ADDRESS, address);
  outl(PCI_CONFIG_DATA, value);
}

// Check if device exists
bool pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
  uint32_t vendor = pci_config_read(bus, device, function, 0);
  return (vendor != 0xFFFFFFFF && (vendor & 0xFFFF) != 0xFFFF);
}

// Get device info
void pci_get_device_info(uint8_t bus, uint8_t device, uint8_t function,
                         pci_device_t *dev) {
  if (!dev)
    return;

  dev->bus = bus;
  dev->device = device;
  dev->function = function;

  // Read vendor and device ID
  uint32_t vendor_device = pci_config_read(bus, device, function, 0x00);
  dev->vendor_id = vendor_device & 0xFFFF;
  dev->device_id = (vendor_device >> 16) & 0xFFFF;

  // Read class code
  uint32_t class_info = pci_config_read(bus, device, function, 0x08);
  dev->revision = class_info & 0xFF;
  dev->prog_if = (class_info >> 8) & 0xFF;
  dev->subclass = (class_info >> 16) & 0xFF;
  dev->class_code = (class_info >> 24) & 0xFF;

  // Read BAR0 and BAR1
  dev->bar0 = pci_config_read(bus, device, function, 0x10);
  dev->bar1 = pci_config_read(bus, device, function, 0x14);

  // Read interrupt line
  uint32_t irq_info = pci_config_read(bus, device, function, 0x3C);
  dev->irq = irq_info & 0xFF;
}

// Check if device is a WiFi card
bool pci_is_wifi_device(pci_device_t *dev) {
  if (!dev)
    return false;

  // Check by class code (Network controller, Wireless)
  if (dev->class_code == PCI_CLASS_NETWORK &&
      dev->subclass == PCI_SUBCLASS_WIFI) {
    return true;
  }

  // Check by known vendor/device combinations
  switch (dev->vendor_id) {
  case PCI_VENDOR_INTEL:
    // Intel WiFi devices
    switch (dev->device_id) {
    case PCI_DEVICE_INTEL_AC7260:
    case PCI_DEVICE_INTEL_AC8260:
    case PCI_DEVICE_INTEL_AC9260:
    case PCI_DEVICE_INTEL_AX200:
    case PCI_DEVICE_INTEL_AX201:
      return true;
    }
    break;

  case PCI_VENDOR_REALTEK:
    // Realtek WiFi devices
    switch (dev->device_id) {
    case PCI_DEVICE_RTL8188EE:
    case PCI_DEVICE_RTL8192EE:
    case PCI_DEVICE_RTL8821AE:
    case PCI_DEVICE_RTL8822BE:
      return true;
    }
    break;

  case PCI_VENDOR_ATHEROS:
    // Atheros WiFi devices
    return true; // Most Atheros devices are WiFi

  case PCI_VENDOR_BROADCOM:
    // Broadcom WiFi devices
    return true;

  case PCI_VENDOR_RALINK:
    // Ralink WiFi devices
    return true;
  }

  return false;
}

// Enumerate all PCI devices
int pci_enumerate_devices(pci_device_t *devices, int max_devices) {
  int count = 0;

  // Scan all buses, devices, and functions
  // Most systems have buses 0-7, scanning all 256 is slow in QEMU
  for (uint16_t bus = 0; bus < 8; bus++) {
    for (uint8_t device = 0; device < 32; device++) {
      for (uint8_t function = 0; function < 8; function++) {
        if (pci_device_exists(bus, device, function)) {
          if (count < max_devices) {
            pci_get_device_info(bus, device, function, &devices[count]);
            count++;
          }

          // If not a multi-function device, skip other functions
          if (function == 0) {
            uint32_t header = pci_config_read(bus, device, 0, 0x0C);
            if ((header & 0x00800000) == 0) {
              break; // Not multi-function
            }
          }
        }
      }
    }
  }

  return count;
}

// Find WiFi devices
int pci_find_wifi_devices(pci_device_t *devices, int max_devices) {
  pci_device_t all_devices[64];
  int total = pci_enumerate_devices(all_devices, 64);
  int wifi_count = 0;

  for (int i = 0; i < total && wifi_count < max_devices; i++) {
    if (pci_is_wifi_device(&all_devices[i])) {
      devices[wifi_count++] = all_devices[i];
    }
  }

  return wifi_count;
}

// Enable PCI device (bus mastering, memory space, I/O space)
void pci_enable_device(pci_device_t *dev) {
  if (!dev)
    return;

  // Read command register
  uint32_t command =
      pci_config_read(dev->bus, dev->device, dev->function, 0x04);

  // Enable I/O Space, Memory Space, and Bus Master
  command |= 0x07;

  pci_config_write(dev->bus, dev->device, dev->function, 0x04, command);
}

// Get device name string
const char *pci_get_device_name(pci_device_t *dev) {
  if (!dev)
    return "Unknown";

  switch (dev->vendor_id) {
  case PCI_VENDOR_INTEL:
    switch (dev->device_id) {
    case PCI_DEVICE_INTEL_AC7260:
      return "Intel Wireless AC 7260";
    case PCI_DEVICE_INTEL_AC8260:
      return "Intel Wireless AC 8260";
    case PCI_DEVICE_INTEL_AC9260:
      return "Intel Wireless AC 9260";
    case PCI_DEVICE_INTEL_AX200:
      return "Intel Wi-Fi 6 AX200";
    case PCI_DEVICE_INTEL_AX201:
      return "Intel Wi-Fi 6 AX201";
    default:
      return "Intel WiFi Adapter";
    }

  case PCI_VENDOR_REALTEK:
    switch (dev->device_id) {
    case PCI_DEVICE_RTL8188EE:
      return "Realtek RTL8188EE";
    case PCI_DEVICE_RTL8192EE:
      return "Realtek RTL8192EE";
    case PCI_DEVICE_RTL8821AE:
      return "Realtek RTL8821AE";
    case PCI_DEVICE_RTL8822BE:
      return "Realtek RTL8822BE";
    default:
      return "Realtek WiFi Adapter";
    }

  case PCI_VENDOR_ATHEROS:
    return "Atheros WiFi Adapter";

  case PCI_VENDOR_BROADCOM:
    return "Broadcom WiFi Adapter";

  case PCI_VENDOR_RALINK:
    return "Ralink WiFi Adapter";

  default:
    return "Unknown WiFi Adapter";
  }
}

// Find specific PCI device by vendor and device ID
pci_device_t pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    pci_device_t devices[64];
    int count = pci_enumerate_devices(devices, 64);
    
    for (int i = 0; i < count; i++) {
        if (devices[i].vendor_id == vendor_id && devices[i].device_id == device_id) {
            return devices[i];
        }
    }
    
    // Not found - return invalid device
    pci_device_t invalid;
    invalid.vendor_id = 0xFFFF;
    invalid.device_id = 0xFFFF;
    return invalid;
}
