/*
 * mouse.c - Comprehensive Mouse Driver
 * 
 * Supports:
 *   - PS/2 Mouse (standard 3-button, IntelliMouse wheel, Explorer 5-button)
 *   - USB Mouse via UHCI (Universal Host Controller Interface)
 *   - USB Mouse via OHCI (Open Host Controller Interface)
 *   - USB Mouse via EHCI (Enhanced Host Controller Interface)
 *   - USB Mouse via xHCI (Extensible Host Controller Interface)
 *   - VMware VMMouse absolute positioning
 *   - VirtualBox Guest absolute positioning
 *   - QEMU/KVM absolute tablet device
 *   - Multi-monitor awareness
 *   - Acceleration curves
 *   - Button debouncing
 *
 * Architecture: x86 (32-bit protected mode, no OS services)
 * 
 * Copyright (c) 2026.
*/

#include "mouse.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Section 1: Low-level I/O primitives */

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
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

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void memory_barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline void cpu_relax(void) {
    __asm__ volatile("pause" ::: "memory");
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Section 2: Memory-mapped I/O helpers */

static inline void mmio_write32(volatile void *addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
    memory_barrier();
}

static inline uint32_t mmio_read32(volatile void *addr) {
    memory_barrier();
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write16(volatile void *addr, uint16_t val) {
    *(volatile uint16_t *)addr = val;
    memory_barrier();
}

static inline uint16_t mmio_read16(volatile void *addr) {
    memory_barrier();
    return *(volatile uint16_t *)addr;
}

static inline void mmio_write8(volatile void *addr, uint8_t val) {
    *(volatile uint8_t *)addr = val;
    memory_barrier();
}

static inline uint8_t mmio_read8(volatile void *addr) {
    memory_barrier();
    return *(volatile uint8_t *)addr;
}

static inline void mmio_write64(volatile void *addr, uint64_t val) {
    volatile uint32_t *p = (volatile uint32_t *)addr;
    p[0] = (uint32_t)(val & 0xFFFFFFFF);
    memory_barrier();
    p[1] = (uint32_t)(val >> 32);
    memory_barrier();
}

static inline uint64_t mmio_read64(volatile void *addr) {
    volatile uint32_t *p = (volatile uint32_t *)addr;
    memory_barrier();
    uint32_t lo = p[0];
    memory_barrier();
    uint32_t hi = p[1];
    return ((uint64_t)hi << 32) | lo;
}

/* Section 3: Simple memory allocator for DMA buffers */

/* We need physically contiguous, aligned memory for USB DMA.
 * This is a simple bump allocator from a reserved region.
 * In a real OS, this would use the physical memory manager. */

#define DMA_POOL_SIZE       (256 * 1024)  /* 256 KB DMA pool */
#define DMA_POOL_BASE_ADDR  0x00200000    /* 2 MB mark - must be identity-mapped */

static uint32_t dma_pool_offset = 0;

static void *dma_alloc(uint32_t size, uint32_t alignment) {
    /* Align offset */
    uint32_t aligned = (dma_pool_offset + alignment - 1) & ~(alignment - 1);
    if (aligned + size > DMA_POOL_SIZE) {
        return NULL; /* Out of DMA memory */
    }
    void *ptr = (void *)(DMA_POOL_BASE_ADDR + aligned);
    dma_pool_offset = aligned + size;
    
    /* Zero the memory */
    uint8_t *p = (uint8_t *)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = 0;
    }
    return ptr;
}

static uint32_t virt_to_phys(void *virt) {
    /* Identity mapping assumed */
    return (uint32_t)(uintptr_t)virt;
}

/* Section 4: PCI Bus Enumeration */

#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

#define PCI_CLASS_SERIAL_BUS  0x0C
#define PCI_SUBCLASS_USB      0x03
#define PCI_PROGIF_UHCI       0x00
#define PCI_PROGIF_OHCI       0x10
#define PCI_PROGIF_EHCI       0x20
#define PCI_PROGIF_XHCI       0x30

#define PCI_MAX_BUS    256
#define PCI_MAX_DEV    32
#define PCI_MAX_FUNC   8

typedef struct {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  irq;
    uint32_t bar[6];
    uint8_t  type;  /* 0=UHCI, 1=OHCI, 2=EHCI, 3=xHCI */
} pci_usb_device_t;

#define MAX_USB_CONTROLLERS 16
static pci_usb_device_t usb_controllers[MAX_USB_CONTROLLERS];
static int usb_controller_count = 0;

static uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_config_read32(bus, dev, func, offset & 0xFC);
    return (uint16_t)(val >> ((offset & 2) * 8));
}

static uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t val = pci_config_read32(bus, dev, func, offset & 0xFC);
    return (uint8_t)(val >> ((offset & 3) * 8));
}

static void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

static void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t old = pci_config_read32(bus, dev, func, offset & 0xFC);
    int shift = (offset & 2) * 8;
    old &= ~(0xFFFF << shift);
    old |= ((uint32_t)val << shift);
    pci_config_write32(bus, dev, func, offset & 0xFC, old);
}

static void pci_config_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t val) {
    uint32_t old = pci_config_read32(bus, dev, func, offset & 0xFC);
    int shift = (offset & 3) * 8;
    old &= ~(0xFF << shift);
    old |= ((uint32_t)val << shift);
    pci_config_write32(bus, dev, func, offset & 0xFC, old);
}

/* Enable PCI bus mastering and memory/IO space */
static void pci_enable_device(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t cmd = pci_config_read16(bus, dev, func, 0x04);
    cmd |= 0x0007; /* IO Space, Memory Space, Bus Master */
    pci_config_write16(bus, dev, func, 0x04, cmd);
}

static void pci_scan_usb(void) {
    usb_controller_count = 0;
    
    for (int bus = 0; bus < PCI_MAX_BUS && usb_controller_count < MAX_USB_CONTROLLERS; bus++) {
        for (int dev = 0; dev < PCI_MAX_DEV && usb_controller_count < MAX_USB_CONTROLLERS; dev++) {
            for (int func = 0; func < PCI_MAX_FUNC && usb_controller_count < MAX_USB_CONTROLLERS; func++) {
                uint32_t reg0 = pci_config_read32(bus, dev, func, 0x00);
                uint16_t vendor = reg0 & 0xFFFF;
                uint16_t device = reg0 >> 16;
                
                if (vendor == 0xFFFF || vendor == 0x0000) {
                    if (func == 0) break;
                    continue;
                }
                
                uint32_t reg2 = pci_config_read32(bus, dev, func, 0x08);
                uint8_t class_code = (reg2 >> 24) & 0xFF;
                uint8_t subclass = (reg2 >> 16) & 0xFF;
                uint8_t prog_if = (reg2 >> 8) & 0xFF;
                
                if (class_code == PCI_CLASS_SERIAL_BUS && subclass == PCI_SUBCLASS_USB) {
                    pci_usb_device_t *ctrl = &usb_controllers[usb_controller_count];
                    ctrl->bus = bus;
                    ctrl->dev = dev;
                    ctrl->func = func;
                    ctrl->vendor_id = vendor;
                    ctrl->device_id = device;
                    ctrl->class_code = class_code;
                    ctrl->subclass = subclass;
                    ctrl->prog_if = prog_if;
                    ctrl->irq = pci_config_read8(bus, dev, func, 0x3C);
                    
                    /* Read BARs */
                    for (int i = 0; i < 6; i++) {
                        ctrl->bar[i] = pci_config_read32(bus, dev, func, 0x10 + i * 4);
                    }
                    
                    switch (prog_if) {
                        case PCI_PROGIF_UHCI: ctrl->type = 0; break;
                        case PCI_PROGIF_OHCI: ctrl->type = 1; break;
                        case PCI_PROGIF_EHCI: ctrl->type = 2; break;
                        case PCI_PROGIF_XHCI: ctrl->type = 3; break;
                        default: ctrl->type = 0xFF; break;
                    }
                    
                    pci_enable_device(bus, dev, func);
                    usb_controller_count++;
                }
                
                /* Check multi-function */
                if (func == 0) {
                    uint8_t header = pci_config_read8(bus, dev, func, 0x0E);
                    if (!(header & 0x80)) break;
                }
            }
        }
    }
}

/* Section 5: Microsecond delay */

static uint64_t tsc_per_us = 0;

static void calibrate_tsc(void) {
    /* Use PIT channel 2 to calibrate TSC */
    /* Program PIT ch2 for one-shot, ~1ms */
    outb(0x61, (inb(0x61) & 0xFD) | 0x01); /* Gate high */
    outb(0x43, 0xB0); /* Ch2, lobyte/hibyte, mode 0, binary */
    /* 1193182 / 1000 ≈ 1193 for 1ms */
    outb(0x42, 0xA9);
    outb(0x42, 0x04);
    
    /* Reset gate to start countdown */
    uint8_t tmp = inb(0x61);
    outb(0x61, tmp & 0xFE);
    outb(0x61, tmp | 0x01);
    
    uint64_t start = rdtsc();
    
    /* Wait for output to go high (bit 5 of port 0x61) */
    while (!(inb(0x61) & 0x20)) {
        cpu_relax();
    }
    
    uint64_t end = rdtsc();
    uint64_t elapsed = end - start;
    
    /* elapsed is ~1ms worth of cycles, so cycles per us = elapsed / 1000 */
    tsc_per_us = elapsed / 1000;
    if (tsc_per_us == 0) tsc_per_us = 1000; /* Fallback: assume ~1GHz */
}

static void udelay(uint32_t us) {
    if (tsc_per_us == 0) {
        /* Fallback: busy loop, approximately */
        for (volatile uint32_t i = 0; i < us * 100; i++) {
            cpu_relax();
        }
        return;
    }
    uint64_t target = rdtsc() + (uint64_t)us * tsc_per_us;
    while (rdtsc() < target) {
        cpu_relax();
    }
}

static void mdelay(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        udelay(1000);
    }
}

/* Section 6: External dependencies */

extern int c_mouse_read(void);
extern int gpu_get_width(void);
extern int gpu_get_height(void);

/* Section 7: Mouse state management */

#define MOUSE_BUTTON_LEFT    0x01
#define MOUSE_BUTTON_RIGHT   0x02
#define MOUSE_BUTTON_MIDDLE  0x04
#define MOUSE_BUTTON_4       0x08
#define MOUSE_BUTTON_5       0x10

#define MOUSE_EVENT_QUEUE_SIZE 256

typedef struct {
    int      x;
    int      y;
    int      dx;
    int      dy;
    int      dz;
    uint8_t  buttons;
    bool     absolute;
    uint64_t timestamp;
} mouse_event_t;

typedef struct {
    /* Current position */
    int x;
    int y;
    
    /* Sub-pixel accumulator (for smooth slow movement) */
    int x_accum;
    int y_accum;
    
    /* Accumulated scroll */
    int z_accum;
    
    /* Sensitivity (multiply by numerator, divide by denominator) */
    int sensitivity_num;
    int sensitivity_den;
    
    /* Button state */
    uint8_t buttons;
    bool    left;
    bool    right;
    bool    middle;
    bool    button4;
    bool    button5;
    
    /* Screen limits */
    int limit_w;
    int limit_h;
    
    /* Acceleration */
    int  accel_numerator;
    int  accel_denominator;
    int  accel_threshold;
    bool accel_enabled;
    
    /* Initialization state */
    bool initialized;
    bool ps2_active;
    bool usb_active;
    bool vmmouse_active;
    bool vbox_active;
    bool qemu_tablet_active;
    bool absolute_mode;
    
    /* PS/2 specific */
    uint8_t ps2_packet[4];
    int     ps2_packet_idx;
    int     ps2_type;  /* 0=standard, 3=intellimouse, 4=explorer */
    
    /* Debounce */
    uint64_t last_left_change;
    uint64_t last_right_change;
    uint64_t debounce_cycles;
    
    /* Event queue */
    mouse_event_t event_queue[MOUSE_EVENT_QUEUE_SIZE];
    int           event_head;
    int           event_tail;
    
    /* Statistics */
    uint64_t total_packets;
    uint64_t dropped_packets;
    uint64_t sync_errors;
    
} mouse_state_t;

static mouse_state_t mouse = {0};

/* Event queue operations */
static void mouse_queue_event(mouse_event_t *evt) {
    int next = (mouse.event_head + 1) % MOUSE_EVENT_QUEUE_SIZE;
    if (next == mouse.event_tail) {
        /* Queue full, drop oldest */
        mouse.event_tail = (mouse.event_tail + 1) % MOUSE_EVENT_QUEUE_SIZE;
        mouse.dropped_packets++;
    }
    mouse.event_queue[mouse.event_head] = *evt;
    mouse.event_head = next;
}

static bool mouse_dequeue_event(mouse_event_t *evt) {
    if (mouse.event_head == mouse.event_tail) return false;
    *evt = mouse.event_queue[mouse.event_tail];
    mouse.event_tail = (mouse.event_tail + 1) % MOUSE_EVENT_QUEUE_SIZE;
    return true;
}

/* Apply acceleration curve */
static int mouse_accelerate(int delta) {
    if (!mouse.accel_enabled) return delta;
    
    int abs_delta = delta < 0 ? -delta : delta;
    int sign = delta < 0 ? -1 : 1;
    
    if (abs_delta > mouse.accel_threshold) {
        /* Above threshold: apply acceleration */
        int excess = abs_delta - mouse.accel_threshold;
        abs_delta = mouse.accel_threshold + 
                    (excess * mouse.accel_numerator) / mouse.accel_denominator;
    }
    
    return abs_delta * sign;
}

/* Apply movement with sub-pixel accumulation to prevent loss of small movements */
static void mouse_apply_movement(int dx, int dy) {
    /* Scale by sensitivity and accumulate fractional parts */
    mouse.x_accum += dx * mouse.sensitivity_num;
    mouse.y_accum += dy * mouse.sensitivity_num;
    
    /* Extract integer pixels */
    int x_pixels = mouse.x_accum / mouse.sensitivity_den;
    int y_pixels = mouse.y_accum / mouse.sensitivity_den;
    
    /* Keep remainder for next update */
    mouse.x_accum -= x_pixels * mouse.sensitivity_den;
    mouse.y_accum -= y_pixels * mouse.sensitivity_den;
    
    /* Apply to position */
    mouse.x += x_pixels;
    mouse.y += y_pixels;
}

/* Clamp mouse position to screen bounds */
static void mouse_clamp_position(void) {
    if (mouse.x < 0) mouse.x = 0;
    if (mouse.x >= mouse.limit_w) mouse.x = mouse.limit_w - 1;
    if (mouse.y < 0) mouse.y = 0;
    if (mouse.y >= mouse.limit_h) mouse.y = mouse.limit_h - 1;
}

/* Update button state with debouncing */
static void mouse_update_buttons(uint8_t new_buttons) {
    uint64_t now = rdtsc();
    
    /* Debounce left button */
    if ((new_buttons & MOUSE_BUTTON_LEFT) != (mouse.buttons & MOUSE_BUTTON_LEFT)) {
        if (now - mouse.last_left_change > mouse.debounce_cycles) {
            mouse.last_left_change = now;
        } else {
            /* Too fast, ignore this change */
            new_buttons = (new_buttons & ~MOUSE_BUTTON_LEFT) | 
                         (mouse.buttons & MOUSE_BUTTON_LEFT);
        }
    }
    
    /* Debounce right button */
    if ((new_buttons & MOUSE_BUTTON_RIGHT) != (mouse.buttons & MOUSE_BUTTON_RIGHT)) {
        if (now - mouse.last_right_change > mouse.debounce_cycles) {
            mouse.last_right_change = now;
        } else {
            new_buttons = (new_buttons & ~MOUSE_BUTTON_RIGHT) | 
                         (mouse.buttons & MOUSE_BUTTON_RIGHT);
        }
    }
    
    mouse.buttons = new_buttons;
    mouse.left    = (new_buttons & MOUSE_BUTTON_LEFT)   ? true : false;
    mouse.right   = (new_buttons & MOUSE_BUTTON_RIGHT)  ? true : false;
    mouse.middle  = (new_buttons & MOUSE_BUTTON_MIDDLE) ? true : false;
    mouse.button4 = (new_buttons & MOUSE_BUTTON_4)      ? true : false;
    mouse.button5 = (new_buttons & MOUSE_BUTTON_5)      ? true : false;
}

/* Section 8: VMware VMMouse (absolute positioning) */

#define VMMOUSE_MAGIC       0x564D5868
#define VMMOUSE_PORT        0x5658
#define VMMOUSE_CMD_GETVERSION  10
#define VMMOUSE_CMD_DATA        39
#define VMMOUSE_CMD_STATUS      40
#define VMMOUSE_CMD_COMMAND     41

static void vmmouse_backdoor(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile(
        "pushl %%ebx       \n\t"
        "movl %5, %%ebx    \n\t"
        "inl %%dx, %%eax   \n\t"
        "movl %%ebx, %1    \n\t"
        "popl %%ebx        \n\t"
        : "=a"(*eax), "=r"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "0"(*eax), "r"(*ebx), "2"(*ecx), "3"(*edx)
        : "memory"
    );
}

static bool vmmouse_detect(void) {
    uint32_t eax = VMMOUSE_MAGIC;
    uint32_t ebx = 0;
    uint32_t ecx = VMMOUSE_CMD_GETVERSION;
    uint32_t edx = VMMOUSE_PORT;
    
    vmmouse_backdoor(&eax, &ebx, &ecx, &edx);
    
    return (ebx == VMMOUSE_MAGIC);
}

static bool vmmouse_enable(void) {
    if (!vmmouse_detect()) return false;
    
    uint32_t eax, ebx, ecx, edx;
    
    /* Send ENABLE command (0x456E6162 = "Enab") */
    eax = VMMOUSE_MAGIC;
    ebx = 0x45414552; /* "READ" - request ID */
    ecx = VMMOUSE_CMD_COMMAND;
    edx = VMMOUSE_PORT;
    vmmouse_backdoor(&eax, &ebx, &ecx, &edx);
    
    /* Enable absolute mode */
    eax = VMMOUSE_MAGIC;
    ebx = 0x53424152; /* "RABS" - request absolute */
    ecx = VMMOUSE_CMD_COMMAND;
    edx = VMMOUSE_PORT;
    vmmouse_backdoor(&eax, &ebx, &ecx, &edx);
    
    return true;
}

static bool vmmouse_poll(void) {
    uint32_t eax, ebx, ecx, edx;
    
    /* Check status */
    eax = VMMOUSE_MAGIC;
    ebx = 0;
    ecx = VMMOUSE_CMD_STATUS;
    edx = VMMOUSE_PORT;
    vmmouse_backdoor(&eax, &ebx, &ecx, &edx);
    
    int count = eax & 0xFFFF;
    if (count < 4) return false;
    
    /* Read 4 dwords: status, X, Y, Z */
    uint32_t status_word, raw_x, raw_y, raw_z;
    
    eax = VMMOUSE_MAGIC; ebx = 0; ecx = VMMOUSE_CMD_DATA; edx = VMMOUSE_PORT;
    vmmouse_backdoor(&eax, &ebx, &ecx, &edx);
    status_word = eax;
    
    eax = VMMOUSE_MAGIC; ebx = 0; ecx = VMMOUSE_CMD_DATA; edx = VMMOUSE_PORT;
    vmmouse_backdoor(&eax, &ebx, &ecx, &edx);
    raw_x = eax;
    
    eax = VMMOUSE_MAGIC; ebx = 0; ecx = VMMOUSE_CMD_DATA; edx = VMMOUSE_PORT;
    vmmouse_backdoor(&eax, &ebx, &ecx, &edx);
    raw_y = eax;
    
    eax = VMMOUSE_MAGIC; ebx = 0; ecx = VMMOUSE_CMD_DATA; edx = VMMOUSE_PORT;
    vmmouse_backdoor(&eax, &ebx, &ecx, &edx);
    raw_z = eax;
    
    /* Check if this is absolute data (bit 16 of status) */
    bool is_absolute = (status_word & 0x00010000) != 0;
    
    if (is_absolute) {
        /* Coordinates are 0..65535, map to screen */
        mouse.x = (int)(((uint64_t)raw_x * (uint64_t)mouse.limit_w) / 65536ULL);
        mouse.y = (int)(((uint64_t)raw_y * (uint64_t)mouse.limit_h) / 65536ULL);
        mouse.absolute_mode = true;
    } else {
        /* Relative mode fallback */
        int dx = (int16_t)(raw_x & 0xFFFF);
        int dy = (int16_t)(raw_y & 0xFFFF);
        dx = mouse_accelerate(dx);
        dy = mouse_accelerate(dy);
        mouse_apply_movement(dx, dy);
    }
    
    mouse_clamp_position();
    
    /* Scroll */
    int8_t dz = (int8_t)(raw_z & 0xFF);
    mouse.z_accum += dz;
    
    /* Buttons: VMMouse uses bit4=left, bit3=right, bit2=middle */
    uint8_t btn = 0;
    if (status_word & 0x20) btn |= MOUSE_BUTTON_LEFT;
    if (status_word & 0x10) btn |= MOUSE_BUTTON_RIGHT;
    if (status_word & 0x08) btn |= MOUSE_BUTTON_MIDDLE;
    mouse_update_buttons(btn);
    
    /* Queue event */
    mouse_event_t evt = {
        .x = mouse.x,
        .y = mouse.y,
        .dx = 0,
        .dy = 0,
        .dz = dz,
        .buttons = mouse.buttons,
        .absolute = is_absolute,
        .timestamp = rdtsc()
    };
    mouse_queue_event(&evt);
    mouse.total_packets++;
    
    /* Drain PS/2 buffer to prevent buildup */
    while (c_mouse_read() != -1);
    
    return true;
}

/* Section 9: VirtualBox Guest Mouse (port 0x5670) */

#define VBOX_MOUSE_PORT_STATUS   0x5670
#define VBOX_MOUSE_PORT_DATA     0x5674

static bool vbox_mouse_detect(void) {
    /* Try VirtualBox PCI device: vendor 0x80EE (InnoTek/Oracle) */
    for (int i = 0; i < usb_controller_count; i++) {
        if (usb_controllers[i].vendor_id == 0x80EE) {
            return true;
        }
    }
    
    /* Also check for VirtualBox Guest Additions PCI device */
    for (int bus = 0; bus < 8; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t reg0 = pci_config_read32(bus, dev, 0, 0x00);
            uint16_t vendor = reg0 & 0xFFFF;
            uint16_t device = reg0 >> 16;
            if (vendor == 0x80EE && device == 0xCAFE) {
                return true;
            }
            if (vendor == 0x80EE && device == 0xBEEF) {
                return true;
            }
        }
    }
    return false;
}

/* Section 10: PS/2 Mouse Driver (robust implementation) */

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_CMD_PORT     0x64

#define PS2_STATUS_OUTPUT_FULL  0x01
#define PS2_STATUS_INPUT_FULL   0x02
#define PS2_STATUS_MOUSE_DATA   0x20

#define PS2_MOUSE_TYPE_STANDARD    0  /* 3-byte packets */
#define PS2_MOUSE_TYPE_INTELLIMOUSE 3  /* 4-byte packets with scroll */
#define PS2_MOUSE_TYPE_EXPLORER    4  /* 4-byte packets with scroll + buttons 4,5 */

/* Wait for PS/2 controller input buffer to be empty */
static bool ps2_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) return true;
        io_wait();
    }
    return false;
}

/* Wait for PS/2 controller output buffer to have data */
static bool ps2_wait_read(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) return true;
        io_wait();
    }
    return false;
}

/* Flush PS/2 controller buffer */
static void ps2_flush(void) {
    int safety = 256;
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) && safety-- > 0) {
        inb(PS2_DATA_PORT);
        io_wait();
    }
    /* Also flush software buffer */
    while (c_mouse_read() != -1);
}

/* Send command to PS/2 controller */
static void ps2_controller_cmd(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_CMD_PORT, cmd);
}

/* Write data to PS/2 controller */
static void ps2_controller_write(uint8_t data) {
    ps2_wait_write();
    outb(PS2_DATA_PORT, data);
}

/* Read data from PS/2 controller (with timeout) */
static int ps2_controller_read(void) {
    if (ps2_wait_read()) {
        return inb(PS2_DATA_PORT);
    }
    return -1;
}

/* Send command to mouse (via aux port) */
static bool ps2_mouse_cmd(uint8_t cmd) {
    ps2_controller_cmd(0xD4);  /* Route next byte to mouse */
    ps2_controller_write(cmd);
    
    /* Wait for ACK (0xFA) */
    int timeout = 50;
    while (timeout-- > 0) {
        int b = c_mouse_read();
        if (b == -1) {
            /* Try direct read */
            if (ps2_wait_read()) {
                b = inb(PS2_DATA_PORT);
            }
        }
        if (b == 0xFA) return true;  /* ACK */
        if (b == 0xFE) return false; /* Resend request = error */
        if (b == 0xFC) return false; /* Error */
        if (b == -1) {
            udelay(1000);
        }
    }
    return false;
}

/* Send command and read response byte */
static int ps2_mouse_cmd_response(uint8_t cmd) {
    if (!ps2_mouse_cmd(cmd)) return -1;
    
    /* Read response */
    int timeout = 50;
    while (timeout-- > 0) {
        int b = c_mouse_read();
        if (b != -1) return b;
        udelay(1000);
    }
    return -1;
}

/* Set mouse sample rate */
static bool ps2_mouse_set_rate(uint8_t rate) {
    if (!ps2_mouse_cmd(0xF3)) return false; /* Set Sample Rate */
    return ps2_mouse_cmd(rate);
}

/* Detect mouse type via IntelliMouse/Explorer knock sequence */
static int ps2_detect_mouse_type(void) {
    /* Try IntelliMouse knock sequence: set rate 200, 100, 80 then read ID */
    ps2_mouse_set_rate(200);
    ps2_mouse_set_rate(100);
    ps2_mouse_set_rate(80);
    
    int id = ps2_mouse_cmd_response(0xF2);
    if (id == 3) {
        /* IntelliMouse detected. Now try Explorer knock: 200, 200, 80 */
        ps2_mouse_set_rate(200);
        ps2_mouse_set_rate(200);
        ps2_mouse_set_rate(80);
        
        id = ps2_mouse_cmd_response(0xF2);
        if (id == 4) {
            return PS2_MOUSE_TYPE_EXPLORER;
        }
        return PS2_MOUSE_TYPE_INTELLIMOUSE;
    }
    return PS2_MOUSE_TYPE_STANDARD;
}

/* Initialize PS/2 mouse */
static bool ps2_mouse_init(void) {
    /* Step 1: Flush any pending data */
    ps2_flush();
    
    /* Step 2: Disable both ports during setup */
    ps2_controller_cmd(0xAD); /* Disable keyboard port */
    ps2_controller_cmd(0xA7); /* Disable mouse port */
    ps2_flush();
    
    /* Step 3: Read controller configuration byte */
    ps2_controller_cmd(0x20);
    int config = ps2_controller_read();
    if (config == -1) config = 0x47; /* Default fallback */
    
    /* Step 4: Modify config: enable IRQ12 (bit1), enable IRQ1 (bit0), 
     * disable clock inhibit for mouse (clear bit5), keep keyboard enabled (clear bit4) */
    config |= 0x03;   /* Enable IRQ1 and IRQ12 */
    config &= ~0x30;  /* Enable both port clocks */
    
    /* Write config back */
    ps2_controller_cmd(0x60);
    ps2_controller_write((uint8_t)config);
    
    /* Step 5: Perform controller self-test */
    ps2_controller_cmd(0xAA);
    int test_result = ps2_controller_read();
    if (test_result == 0x55) {
        /* Self-test passed. Re-write config as self-test may reset it */
        ps2_controller_cmd(0x60);
        ps2_controller_write((uint8_t)config);
    }
    /* If self-test fails, continue anyway - some hardware doesn't support it properly */
    
    /* Step 6: Test mouse port */
    ps2_controller_cmd(0xA9);
    int port_test = ps2_controller_read();
    /* 0x00 = success. Continue even on failure. */
    (void)port_test;
    
    /* Step 7: Enable mouse port */
    ps2_controller_cmd(0xA8);
    
    /* Step 8: Reset mouse */
    ps2_flush();
    if (ps2_mouse_cmd(0xFF)) {
        /* Wait for self-test result */
        mdelay(500);
        /* Read self-test response: should be 0xAA then mouse ID */
        int st = -1;
        for (int i = 0; i < 100; i++) {
            int b = c_mouse_read();
            if (b == 0xAA) { st = b; break; }
            if (b == -1) udelay(10000);
        }
        if (st == 0xAA) {
            /* Read mouse ID (usually 0x00) */
            for (int i = 0; i < 50; i++) {
                int b = c_mouse_read();
                if (b != -1) break;
                udelay(10000);
            }
        }
    }
    
    /* Step 9: Set defaults */
    ps2_mouse_cmd(0xF6);
    
    /* Step 10: Detect mouse type (standard/intellimouse/explorer) */
    mouse.ps2_type = ps2_detect_mouse_type();
    
    /* Step 11: Set sample rate to 100 Hz for responsiveness */
    ps2_mouse_set_rate(100);
    
    /* Step 12: Set resolution (2 = 4 counts/mm) */
    ps2_mouse_cmd(0xE8);
    ps2_mouse_cmd(0x02);
    
    /* Step 13: Set scaling 1:1 */
    ps2_mouse_cmd(0xE6);
    
    /* Step 14: Enable data reporting */
    ps2_mouse_cmd(0xF4);
    
    /* Step 15: Re-enable keyboard */
    ps2_controller_cmd(0xAE);
    
    /* Clear any accumulated data */
    ps2_flush();
    
    mouse.ps2_packet_idx = 0;
    mouse.ps2_active = true;
    
    return true;
}

/* Process a complete PS/2 packet */
static void ps2_process_packet(void) {
    uint8_t status_byte = mouse.ps2_packet[0];
    int16_t dx = (int16_t)mouse.ps2_packet[1];
    int16_t dy = (int16_t)mouse.ps2_packet[2];
    int8_t dz = 0;
    uint8_t extra_buttons = 0;
    
    /* Sign extension based on status byte bits */
    if (status_byte & 0x10) dx |= 0xFF00; /* X sign bit */
    if (status_byte & 0x20) dy |= 0xFF00; /* Y sign bit */
    
    /* Overflow check - discard if overflow bits set */
    if (status_byte & 0xC0) {
        mouse.sync_errors++;
        return;
    }
    
    /* Process 4th byte for wheel/explorer mice */
    if (mouse.ps2_type == PS2_MOUSE_TYPE_INTELLIMOUSE) {
        dz = (int8_t)mouse.ps2_packet[3];
        /* IntelliMouse: dz is full signed byte but typically -1/+1 */
    } else if (mouse.ps2_type == PS2_MOUSE_TYPE_EXPLORER) {
        /* Explorer: lower 4 bits = signed scroll, bits 4-5 = buttons 4,5 */
        dz = (int8_t)(mouse.ps2_packet[3] & 0x0F);
        if (dz & 0x08) dz |= 0xF0; /* Sign extend 4-bit value */
        if (mouse.ps2_packet[3] & 0x10) extra_buttons |= MOUSE_BUTTON_4;
        if (mouse.ps2_packet[3] & 0x20) extra_buttons |= MOUSE_BUTTON_5;
    }
    
    /* Build button state */
    uint8_t btn = extra_buttons;
    if (status_byte & 0x01) btn |= MOUSE_BUTTON_LEFT;
    if (status_byte & 0x02) btn |= MOUSE_BUTTON_RIGHT;
    if (status_byte & 0x04) btn |= MOUSE_BUTTON_MIDDLE;
    
    /* Apply acceleration */
    int accel_dx = mouse_accelerate(dx);
    int accel_dy = mouse_accelerate(dy);
    
    /* Update position (PS/2: positive Y is up, screen Y is down) */
    mouse_apply_movement(accel_dx, -accel_dy);
    mouse_clamp_position();
    
    /* Scroll */
    mouse.z_accum += dz;
    
    /* Buttons */
    mouse_update_buttons(btn);
    
    /* Queue event */
    mouse_event_t evt = {
        .x = mouse.x,
        .y = mouse.y,
        .dx = accel_dx,
        .dy = -accel_dy,
        .dz = dz,
        .buttons = mouse.buttons,
        .absolute = false,
        .timestamp = rdtsc()
    };
    mouse_queue_event(&evt);
    mouse.total_packets++;
}

/* Feed bytes to PS/2 packet state machine */
static void ps2_feed_byte(uint8_t byte) {
    int packet_size = (mouse.ps2_type == PS2_MOUSE_TYPE_STANDARD) ? 3 : 4;
    
    if (mouse.ps2_packet_idx == 0) {
        /* First byte: must have bit 3 set (always 1 in PS/2 protocol) 
         * and bits 6,7 should be 0 (no overflow) */
        if ((byte & 0x08) != 0x08) {
            /* Sync error - this is not a valid first byte */
            mouse.sync_errors++;
            return;
        }
        if (byte & 0xC0) {
            /* Overflow bits set - skip this packet */
            mouse.sync_errors++;
            return;
        }
        mouse.ps2_packet[0] = byte;
        mouse.ps2_packet_idx = 1;
    } else if (mouse.ps2_packet_idx < packet_size) {
        mouse.ps2_packet[mouse.ps2_packet_idx] = byte;
        mouse.ps2_packet_idx++;
        
        if (mouse.ps2_packet_idx == packet_size) {
            /* Complete packet - verify consistency */
            uint8_t status = mouse.ps2_packet[0];
            int dx_raw = (int8_t)mouse.ps2_packet[1];
            int dy_raw = (int8_t)mouse.ps2_packet[2];
            
            /* Verify sign bits match actual sign of movement */
            bool x_sign = (status & 0x10) != 0;
            bool y_sign = (status & 0x20) != 0;
            bool dx_neg = (dx_raw < 0);
            bool dy_neg = (dy_raw < 0);
            
            if (x_sign == dx_neg && y_sign == dy_neg) {
                ps2_process_packet();
            } else if (dx_raw == 0 && dy_raw == 0) {
                /* Zero movement is always valid regardless of sign bits */
                ps2_process_packet();
            } else {
                mouse.sync_errors++;
            }
            
            mouse.ps2_packet_idx = 0;
        }
    }
}

/* Poll PS/2 mouse */
static void ps2_mouse_poll(void) {
    if (!mouse.ps2_active) return;
    
    int max_bytes = 256; /* Process up to 256 bytes per poll to prevent infinite loop */
    while (max_bytes-- > 0) {
        int b = c_mouse_read();
        if (b == -1) break;
        ps2_feed_byte((uint8_t)b);
    }
}

/* Section 11: USB Data Structures */

/* USB standard request types */
#define USB_DIR_IN              0x80
#define USB_DIR_OUT             0x00
#define USB_TYPE_STANDARD       0x00
#define USB_TYPE_CLASS          0x20
#define USB_RECIP_DEVICE       0x00
#define USB_RECIP_INTERFACE    0x01
#define USB_RECIP_ENDPOINT     0x02

/* USB standard requests */
#define USB_REQ_GET_STATUS      0x00
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_CONFIG      0x09
#define USB_REQ_SET_INTERFACE   0x0B

/* USB HID requests */
#define USB_REQ_GET_REPORT      0x01
#define USB_REQ_SET_IDLE        0x0A
#define USB_REQ_SET_PROTOCOL    0x0B

/* USB descriptor types */
#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIGURATION  0x02
#define USB_DESC_STRING         0x03
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05
#define USB_DESC_HID            0x21
#define USB_DESC_HID_REPORT     0x22

/* USB HID class/subclass/protocol */
#define USB_CLASS_HID           0x03
#define USB_SUBCLASS_BOOT       0x01
#define USB_PROTOCOL_MOUSE      0x02

/* USB PID tokens */
#define USB_PID_SETUP           0x2D
#define USB_PID_IN              0x69
#define USB_PID_OUT             0xE1
#define USB_PID_DATA0           0xC3
#define USB_PID_DATA1           0x4B
#define USB_PID_ACK             0xD2
#define USB_PID_NAK             0x5A
#define USB_PID_STALL           0x1E

typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_descriptor_t;

/* Section 12: USB Mouse Device State */

#define MAX_USB_MICE 4

typedef struct {
    bool     active;
    uint8_t  controller_type; /* 0=UHCI, 1=OHCI, 2=EHCI, 3=xHCI */
    int      controller_idx;
    uint8_t  address;
    uint8_t  endpoint;
    uint8_t  max_packet_size;
    uint8_t  interval;
    uint8_t  interface_num;
    bool     boot_protocol;
    bool     is_absolute;
    
    /* Transfer state */
    void    *transfer_buffer;
    uint32_t transfer_phys;
    void    *td_pool;          /* Controller-specific TD/QH/TRB memory */
    uint32_t td_pool_phys;
    int      toggle;           /* Data toggle 0/1 */
    uint64_t last_poll_time;
    uint32_t poll_interval_us;
    
    /* Report parsing state */
    uint8_t  last_report[8];
    bool     has_report;
} usb_mouse_device_t;

static usb_mouse_device_t usb_mice[MAX_USB_MICE];
static int usb_mouse_count = 0;
static uint8_t next_usb_address = 1;

/* Section 13: UHCI (Universal Host Controller Interface) */

/* UHCI register offsets (I/O space) */
#define UHCI_USBCMD       0x00
#define UHCI_USBSTS       0x02
#define UHCI_USBINTR      0x04
#define UHCI_FRNUM        0x06
#define UHCI_FRBASEADD    0x08
#define UHCI_SOFMOD       0x0C
#define UHCI_PORTSC1      0x10
#define UHCI_PORTSC2      0x12

/* UHCI command bits */
#define UHCI_CMD_RS        0x0001  /* Run/Stop */
#define UHCI_CMD_HCRESET   0x0002  /* Host Controller Reset */
#define UHCI_CMD_GRESET    0x0004  /* Global Reset */
#define UHCI_CMD_MAXP      0x0080  /* Max Packet = 64 */

/* UHCI status bits */
#define UHCI_STS_USBINT    0x0001
#define UHCI_STS_ERROR     0x0002
#define UHCI_STS_RD        0x0004
#define UHCI_STS_HSE       0x0008
#define UHCI_STS_HCPE      0x0010
#define UHCI_STS_HCH       0x0020

/* UHCI port status bits */
#define UHCI_PORT_CCS      0x0001  /* Current Connect Status */
#define UHCI_PORT_CSC      0x0002  /* Connect Status Change */
#define UHCI_PORT_PE       0x0004  /* Port Enable */
#define UHCI_PORT_PEC      0x0008  /* Port Enable Change */
#define UHCI_PORT_LSDA     0x0100  /* Low Speed Device Attached */
#define UHCI_PORT_RESET    0x0200  /* Port Reset */

/* UHCI Transfer Descriptor */
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t link;       /* Link pointer (physical) */
    uint32_t status;     /* Status and control */
    uint32_t token;      /* PID, device addr, endpoint, data toggle, max len */
    uint32_t buffer;     /* Buffer pointer (physical) */
    /* Software fields (not read by hardware) */
    uint32_t sw_next;
    uint32_t sw_buffer_virt;
    uint32_t sw_pad[2];
} uhci_td_t;

/* UHCI Queue Head */
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t head_link;   /* Horizontal link */
    uint32_t element;     /* Element link (first TD) */
    /* Software fields */
    uint32_t sw_pad[2];
} uhci_qh_t;

/* UHCI controller state */
typedef struct {
    uint16_t io_base;
    uint32_t *frame_list;     /* 1024-entry frame list (4KB aligned) */
    uint32_t  frame_list_phys;
    uhci_qh_t *async_qh;     /* Async schedule queue head */
    uint32_t   async_qh_phys;
    uhci_td_t *td_pool;      /* Pool of TDs */
    uint32_t   td_pool_phys;
    int        td_pool_used;
    bool       initialized;
    int        num_ports;
} uhci_controller_t;

#define MAX_UHCI_CONTROLLERS 4
#define UHCI_TD_POOL_SIZE    64
#define UHCI_FRAME_LIST_SIZE 1024

static uhci_controller_t uhci_controllers[MAX_UHCI_CONTROLLERS];
static int uhci_controller_count = 0;

/* UHCI TD status/control field bits */
#define UHCI_TD_STATUS_ACTIVE    (1 << 23)
#define UHCI_TD_STATUS_STALLED   (1 << 22)
#define UHCI_TD_STATUS_DATA_ERR  (1 << 21)
#define UHCI_TD_STATUS_BABBLE    (1 << 20)
#define UHCI_TD_STATUS_NAK       (1 << 19)
#define UHCI_TD_STATUS_CRC_ERR   (1 << 18)
#define UHCI_TD_STATUS_BITSTUFF  (1 << 17)
#define UHCI_TD_IOC              (1 << 24)
#define UHCI_TD_ISO              (1 << 25)
#define UHCI_TD_LS               (1 << 26)
#define UHCI_TD_SPD              (1 << 29)

#define UHCI_TD_LINK_TERMINATE   0x01
#define UHCI_TD_LINK_QH          0x02
#define UHCI_TD_LINK_DEPTH       0x04

/* Build UHCI token field */
static uint32_t uhci_td_token(uint8_t pid, uint8_t addr, uint8_t endp, 
                               uint8_t toggle, uint16_t max_len) {
    uint32_t actual_len = (max_len > 0) ? (max_len - 1) : 0x7FF;
    return (actual_len << 21) | ((uint32_t)toggle << 19) | 
           ((uint32_t)endp << 15) | ((uint32_t)addr << 8) | pid;
}

static void uhci_reset(uhci_controller_t *hc) {
    /* Global reset */
    outw(hc->io_base + UHCI_USBCMD, UHCI_CMD_GRESET);
    mdelay(50);
    outw(hc->io_base + UHCI_USBCMD, 0);
    mdelay(10);
    
    /* Host controller reset */
    outw(hc->io_base + UHCI_USBCMD, UHCI_CMD_HCRESET);
    mdelay(50);
    
    /* Wait for reset complete */
    int timeout = 100;
    while ((inw(hc->io_base + UHCI_USBCMD) & UHCI_CMD_HCRESET) && timeout-- > 0) {
        mdelay(1);
    }
    
    /* Clear status */
    outw(hc->io_base + UHCI_USBSTS, 0xFFFF);
}

static bool uhci_init_controller(int pci_idx) {
    if (uhci_controller_count >= MAX_UHCI_CONTROLLERS) return false;
    
    pci_usb_device_t *pci = &usb_controllers[pci_idx];
    uhci_controller_t *hc = &uhci_controllers[uhci_controller_count];
    
    /* UHCI uses I/O BAR (BAR4 usually, but could be BAR0-BAR5) */
    uint16_t io_base = 0;
    for (int i = 0; i < 6; i++) {
        if (pci->bar[i] & 0x01) { /* I/O space */
            io_base = pci->bar[i] & 0xFFFC;
            break;
        }
    }
    if (io_base == 0) return false;
    
    hc->io_base = io_base;
    
    /* Disable legacy support - write to LEGSUP register (PCI config 0xC0) */
    pci_config_write16(pci->bus, pci->dev, pci->func, 0xC0, 0x8F00);
    
    /* Reset controller */
    uhci_reset(hc);
    
    /* Allocate frame list (4KB aligned, 4KB size) */
    hc->frame_list = (uint32_t *)dma_alloc(4096, 4096);
    if (!hc->frame_list) return false;
    hc->frame_list_phys = virt_to_phys(hc->frame_list);
    
    /* Allocate async QH */
    hc->async_qh = (uhci_qh_t *)dma_alloc(sizeof(uhci_qh_t), 16);
    if (!hc->async_qh) return false;
    hc->async_qh_phys = virt_to_phys(hc->async_qh);
    hc->async_qh->head_link = UHCI_TD_LINK_TERMINATE;
    hc->async_qh->element = UHCI_TD_LINK_TERMINATE;
    
    /* Allocate TD pool */
    hc->td_pool = (uhci_td_t *)dma_alloc(sizeof(uhci_td_t) * UHCI_TD_POOL_SIZE, 16);
    if (!hc->td_pool) return false;
    hc->td_pool_phys = virt_to_phys(hc->td_pool);
    hc->td_pool_used = 0;
    
    /* Initialize frame list: all entries point to async QH */
    for (int i = 0; i < UHCI_FRAME_LIST_SIZE; i++) {
        hc->frame_list[i] = hc->async_qh_phys | UHCI_TD_LINK_QH;
    }
    
    /* Set frame list base address */
    outl(hc->io_base + UHCI_FRBASEADD, hc->frame_list_phys);
    
    /* Set frame number to 0 */
    outw(hc->io_base + UHCI_FRNUM, 0);
    
    /* Set SOF timing */
    outb(hc->io_base + UHCI_SOFMOD, 0x40);
    
    /* Clear status */
    outw(hc->io_base + UHCI_USBSTS, 0xFFFF);
    
    /* Enable interrupts (or not - we poll) */
    outw(hc->io_base + UHCI_USBINTR, 0x0000);
    
    /* Start controller */
    outw(hc->io_base + UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_MAXP);
    mdelay(10);
    
    /* Determine number of ports (UHCI always has 2 root ports per controller,
     * but check by reading port registers) */
    hc->num_ports = 2;
    
    hc->initialized = true;
    uhci_controller_count++;
    
    return true;
}

/* Allocate a TD from pool */
static uhci_td_t *uhci_alloc_td(uhci_controller_t *hc) {
    if (hc->td_pool_used >= UHCI_TD_POOL_SIZE) return NULL;
    uhci_td_t *td = &hc->td_pool[hc->td_pool_used++];
    td->link = UHCI_TD_LINK_TERMINATE;
    td->status = 0;
    td->token = 0;
    td->buffer = 0;
    return td;
}

/* Execute a synchronous USB transfer via UHCI */
static int uhci_transfer(uhci_controller_t *hc, uint8_t addr, uint8_t endp,
                          uint8_t pid, uint8_t toggle, bool low_speed,
                          void *buffer, uint16_t length) {
    uhci_td_t *td = uhci_alloc_td(hc);
    if (!td) return -1;
    
    uint32_t td_phys = virt_to_phys(td);
    
    /* Setup TD */
    td->link = UHCI_TD_LINK_TERMINATE;
    td->status = UHCI_TD_STATUS_ACTIVE | (3 << 27); /* Active, 3 error retries */
    if (low_speed) td->status |= UHCI_TD_LS;
    td->token = uhci_td_token(pid, addr, endp, toggle, length);
    td->buffer = buffer ? virt_to_phys(buffer) : 0;
    
    /* Insert into async QH */
    hc->async_qh->element = td_phys;
    memory_barrier();
    
    /* Wait for completion */
    int timeout = 5000; /* 5 seconds */
    while (timeout-- > 0) {
        memory_barrier();
        if (!(td->status & UHCI_TD_STATUS_ACTIVE)) break;
        mdelay(1);
    }
    
    /* Remove from schedule */
    hc->async_qh->element = UHCI_TD_LINK_TERMINATE;
    memory_barrier();
    
    if (td->status & UHCI_TD_STATUS_ACTIVE) {
        return -2; /* Timeout */
    }
    if (td->status & (UHCI_TD_STATUS_STALLED | UHCI_TD_STATUS_DATA_ERR |
                       UHCI_TD_STATUS_BABBLE | UHCI_TD_STATUS_CRC_ERR |
                       UHCI_TD_STATUS_BITSTUFF)) {
        return -3; /* Error */
    }
    
    /* Return actual length transferred */
    int actual = ((td->status + 1) & 0x7FF);
    if (actual == 0x800) actual = 0;
    
    /* Free TD */
    hc->td_pool_used--;
    
    return actual;
}

/* Execute a control transfer via UHCI */
static int uhci_control_transfer(uhci_controller_t *hc, uint8_t addr, bool low_speed,
                                  usb_setup_packet_t *setup, void *data, uint16_t data_len) {
    /* We need up to 3 TDs: SETUP, DATA (optional), STATUS */
    int td_start = hc->td_pool_used;
    
    /* SETUP TD */
    uhci_td_t *setup_td = uhci_alloc_td(hc);
    if (!setup_td) return -1;
    
    void *setup_dma = dma_alloc(8, 16);
    if (!setup_dma) return -1;
    
    uint8_t *src = (uint8_t *)setup;
    uint8_t *dst = (uint8_t *)setup_dma;
    for (int i = 0; i < 8; i++) dst[i] = src[i];
    
    setup_td->status = UHCI_TD_STATUS_ACTIVE | (3 << 27);
    if (low_speed) setup_td->status |= UHCI_TD_LS;
    setup_td->token = uhci_td_token(USB_PID_SETUP, addr, 0, 0, 8);
    setup_td->buffer = virt_to_phys(setup_dma);
    
    uhci_td_t *prev_td = setup_td;
    uint8_t data_toggle = 1;
    
    /* DATA TDs */
    void *data_dma = NULL;
    if (data_len > 0 && data) {
        data_dma = dma_alloc(data_len, 16);
        if (!data_dma) return -1;
        
        if (!(setup->bmRequestType & USB_DIR_IN)) {
            /* OUT: copy data to DMA buffer */
            uint8_t *s = (uint8_t *)data;
            uint8_t *d = (uint8_t *)data_dma;
            for (int i = 0; i < data_len; i++) d[i] = s[i];
        }
        
        uint8_t data_pid = (setup->bmRequestType & USB_DIR_IN) ? USB_PID_IN : USB_PID_OUT;
        uint16_t remaining = data_len;
        uint32_t offset = 0;
        
        while (remaining > 0) {
            uint16_t chunk = remaining > 8 ? 8 : remaining; /* Use max packet size 8 for control EP0 */
            uhci_td_t *data_td = uhci_alloc_td(hc);
            if (!data_td) return -1;
            
            data_td->status = UHCI_TD_STATUS_ACTIVE | (3 << 27);
            if (low_speed) data_td->status |= UHCI_TD_LS;
            data_td->token = uhci_td_token(data_pid, addr, 0, data_toggle, chunk);
            data_td->buffer = virt_to_phys(data_dma) + offset;
            
            prev_td->link = virt_to_phys(data_td) | UHCI_TD_LINK_DEPTH;
            prev_td = data_td;
            
            data_toggle ^= 1;
            offset += chunk;
            remaining -= chunk;
        }
    }
    
    /* STATUS TD */
    uhci_td_t *status_td = uhci_alloc_td(hc);
    if (!status_td) return -1;
    
    uint8_t status_pid = (setup->bmRequestType & USB_DIR_IN) ? USB_PID_OUT : USB_PID_IN;
    status_td->status = UHCI_TD_STATUS_ACTIVE | (3 << 27) | UHCI_TD_IOC;
    if (low_speed) status_td->status |= UHCI_TD_LS;
    status_td->token = uhci_td_token(status_pid, addr, 0, 1, 0);
    status_td->buffer = 0;
    
    prev_td->link = virt_to_phys(status_td) | UHCI_TD_LINK_DEPTH;
    
    /* Insert into schedule */
    hc->async_qh->element = virt_to_phys(setup_td);
    memory_barrier();
    
    /* Wait for completion of status TD */
    int timeout = 5000;
    while (timeout-- > 0) {
        memory_barrier();
        if (!(status_td->status & UHCI_TD_STATUS_ACTIVE)) break;
        mdelay(1);
    }
    
    /* Remove from schedule */
    hc->async_qh->element = UHCI_TD_LINK_TERMINATE;
    memory_barrier();
    
    int result = 0;
    if (status_td->status & UHCI_TD_STATUS_ACTIVE) {
        result = -2; /* Timeout */
    } else if (status_td->status & (UHCI_TD_STATUS_STALLED | UHCI_TD_STATUS_DATA_ERR)) {
        result = -3; /* Error */
    } else if (data_dma && (setup->bmRequestType & USB_DIR_IN)) {
        /* Copy received data */
        uint8_t *s = (uint8_t *)data_dma;
        uint8_t *d = (uint8_t *)data;
        for (int i = 0; i < data_len; i++) d[i] = s[i];
        result = data_len;
    } else {
        result = 0;
    }
    
    /* Free TDs */
    hc->td_pool_used = td_start;
    
    return result;
}

/* Reset and enable a UHCI port */
static bool uhci_port_reset(uhci_controller_t *hc, int port) {
    uint16_t port_reg = hc->io_base + UHCI_PORTSC1 + (port * 2);
    
    /* Check connection */
    uint16_t status = inw(port_reg);
    if (!(status & UHCI_PORT_CCS)) return false; /* No device */
    
    /* Reset port */
    outw(port_reg, UHCI_PORT_RESET);
    mdelay(50);
    outw(port_reg, 0);
    mdelay(10);
    
    /* Enable port */
    for (int i = 0; i < 10; i++) {
        status = inw(port_reg);
        if (status & UHCI_PORT_CCS) {
            /* Clear change bits, enable port */
            outw(port_reg, UHCI_PORT_PE | UHCI_PORT_CSC | UHCI_PORT_PEC);
            mdelay(10);
            
            status = inw(port_reg);
            if (status & UHCI_PORT_PE) return true;
        }
        mdelay(10);
    }
    
    return false;
}

/* Check if a device on UHCI port is low-speed */
static bool uhci_port_is_low_speed(uhci_controller_t *hc, int port) {
    uint16_t port_reg = hc->io_base + UHCI_PORTSC1 + (port * 2);
    return (inw(port_reg) & UHCI_PORT_LSDA) != 0;
}

/* Setup interrupt polling for UHCI mouse */
static void uhci_setup_interrupt_polling(uhci_controller_t *hc, usb_mouse_device_t *dev) {
    /* Allocate a QH and TD for periodic interrupt transfers */
    uhci_qh_t *qh = (uhci_qh_t *)dma_alloc(sizeof(uhci_qh_t), 16);
    uhci_td_t *td = (uhci_td_t *)dma_alloc(sizeof(uhci_td_t), 16);
    if (!qh || !td) return;
    
    dev->transfer_buffer = dma_alloc(dev->max_packet_size, 16);
    if (!dev->transfer_buffer) return;
    dev->transfer_phys = virt_to_phys(dev->transfer_buffer);
    
    dev->td_pool = td;
    dev->td_pool_phys = virt_to_phys(td);
    
    /* Setup TD for IN interrupt transfer */
    td->link = UHCI_TD_LINK_TERMINATE;
    td->status = UHCI_TD_STATUS_ACTIVE | (3 << 27) | UHCI_TD_IOC;
    if (uhci_port_is_low_speed(hc, 0)) td->status |= UHCI_TD_LS;
    td->token = uhci_td_token(USB_PID_IN, dev->address, dev->endpoint, 
                               dev->toggle, dev->max_packet_size);
    td->buffer = dev->transfer_phys;
    
    /* Setup QH */
    qh->head_link = UHCI_TD_LINK_TERMINATE;
    qh->element = virt_to_phys(td);
    
    /* Insert QH into frame list at appropriate interval */
    uint32_t qh_phys = virt_to_phys(qh);
    int interval = dev->interval;
    if (interval < 1) interval = 1;
    if (interval > 128) interval = 128;
    
    /* Round interval down to power of 2 */
    int sched_interval = 1;
    while (sched_interval * 2 <= interval && sched_interval < 128) {
        sched_interval *= 2;
    }
    
    /* Insert into every Nth frame */
    for (int i = 0; i < UHCI_FRAME_LIST_SIZE; i += sched_interval) {
        uint32_t old_link = hc->frame_list[i];
        qh->head_link = old_link;
        hc->frame_list[i] = qh_phys | UHCI_TD_LINK_QH;
    }
    memory_barrier();
}

/* Poll UHCI interrupt transfer for mouse data */
static void uhci_poll_mouse(uhci_controller_t *hc, usb_mouse_device_t *dev) {
    if (!dev->td_pool || !dev->transfer_buffer) return;
    
    uhci_td_t *td = (uhci_td_t *)dev->td_pool;
    
    memory_barrier();
    if (td->status & UHCI_TD_STATUS_ACTIVE) return; /* Still pending */
    
    if (td->status & UHCI_TD_STATUS_STALLED) {
        /* Stall - clear and resubmit */
        goto resubmit;
    }
    
    if (!(td->status & (UHCI_TD_STATUS_DATA_ERR | UHCI_TD_STATUS_CRC_ERR |
                         UHCI_TD_STATUS_BABBLE | UHCI_TD_STATUS_BITSTUFF))) {
        /* Successful transfer! */
        int actual_len = ((td->status + 1) & 0x7FF);
        if (actual_len == 0x800) actual_len = 0;
        
        if (actual_len >= 3) {
            uint8_t *report = (uint8_t *)dev->transfer_buffer;
            
            /* Parse boot protocol mouse report:
             * Byte 0: Buttons (bit0=left, bit1=right, bit2=middle)
             * Byte 1: X movement (signed)
             * Byte 2: Y movement (signed)
             * Byte 3: Wheel (signed, if present) */
            uint8_t btn = 0;
            if (report[0] & 0x01) btn |= MOUSE_BUTTON_LEFT;
            if (report[0] & 0x02) btn |= MOUSE_BUTTON_RIGHT;
            if (report[0] & 0x04) btn |= MOUSE_BUTTON_MIDDLE;
            if (report[0] & 0x08) btn |= MOUSE_BUTTON_4;
            if (report[0] & 0x10) btn |= MOUSE_BUTTON_5;
            
            int dx = (int8_t)report[1];
            int dy = (int8_t)report[2];
            int dz = (actual_len >= 4) ? (int8_t)report[3] : 0;
            
            /* Apply acceleration */
            dx = mouse_accelerate(dx);
            dy = mouse_accelerate(dy);
            
            /* Update global state */
            mouse_apply_movement(dx, dy);
            mouse_clamp_position();
            mouse.z_accum += dz;
            mouse_update_buttons(btn);
            
            mouse_event_t evt = {
                .x = mouse.x,
                .y = mouse.y,
                .dx = dx,
                .dy = dy,
                .dz = dz,
                .buttons = mouse.buttons,
                .absolute = false,
                .timestamp = rdtsc()
            };
            mouse_queue_event(&evt);
            mouse.total_packets++;
            
            /* Store report */
            for (int i = 0; i < 8 && i < actual_len; i++) {
                dev->last_report[i] = report[i];
            }
            dev->has_report = true;
        }
    }
    
resubmit:
    /* Toggle data toggle */
    dev->toggle ^= 1;
    
    /* Resubmit TD */
    td->status = UHCI_TD_STATUS_ACTIVE | (3 << 27) | UHCI_TD_IOC;
    td->token = uhci_td_token(USB_PID_IN, dev->address, dev->endpoint,
                               dev->toggle, dev->max_packet_size);
    td->buffer = dev->transfer_phys;
    memory_barrier();
}

/* Section 14: OHCI (Open Host Controller Interface) */

/* OHCI register offsets (MMIO) */
#define OHCI_REVISION        0x00
#define OHCI_CONTROL         0x04
#define OHCI_CMDSTATUS       0x08
#define OHCI_INTRSTATUS      0x0C
#define OHCI_INTRENABLE      0x10
#define OHCI_INTRDISABLE     0x14
#define OHCI_HCCA            0x18
#define OHCI_PERIOD_CUR_ED   0x1C
#define OHCI_CTRL_HEAD_ED    0x20
#define OHCI_CTRL_CUR_ED     0x24
#define OHCI_BULK_HEAD_ED    0x28
#define OHCI_BULK_CUR_ED     0x2C
#define OHCI_DONE_HEAD       0x30
#define OHCI_FMINTERVAL      0x34
#define OHCI_FMREMAINING     0x38
#define OHCI_FMNUMBER        0x3C
#define OHCI_PERIODICSTART   0x40
#define OHCI_LSTHRESHOLD     0x44
#define OHCI_RH_DESCRIPTORA  0x48
#define OHCI_RH_DESCRIPTORB  0x4C
#define OHCI_RH_STATUS       0x50
#define OHCI_RH_PORT_STATUS  0x54  /* + port*4 */

/* OHCI control register bits */
#define OHCI_CTRL_CBSR_MASK  0x03
#define OHCI_CTRL_PLE        (1 << 2)  /* Periodic List Enable */
#define OHCI_CTRL_IE         (1 << 3)  /* Isochronous Enable */
#define OHCI_CTRL_CLE        (1 << 4)  /* Control List Enable */
#define OHCI_CTRL_BLE        (1 << 5)  /* Bulk List Enable */
#define OHCI_CTRL_HCFS_MASK  (3 << 6)
#define OHCI_CTRL_HCFS_RESET (0 << 6)
#define OHCI_CTRL_HCFS_RESUME (1 << 6)
#define OHCI_CTRL_HCFS_OPER  (2 << 6)
#define OHCI_CTRL_HCFS_SUSP  (3 << 6)

/* OHCI command status bits */
#define OHCI_CMDSTATUS_HCR   (1 << 0)  /* Host Controller Reset */
#define OHCI_CMDSTATUS_CLF   (1 << 1)  /* Control List Filled */
#define OHCI_CMDSTATUS_BLF   (1 << 2)  /* Bulk List Filled */

/* OHCI port status bits */
#define OHCI_PORT_CCS        (1 << 0)   /* Current Connect Status */
#define OHCI_PORT_PES        (1 << 1)   /* Port Enable Status */
#define OHCI_PORT_PSS        (1 << 2)   /* Port Suspend Status */
#define OHCI_PORT_POCI       (1 << 3)   /* Port Over Current */
#define OHCI_PORT_PRS        (1 << 4)   /* Port Reset Status */
#define OHCI_PORT_PPS        (1 << 8)   /* Port Power Status */
#define OHCI_PORT_LSDA       (1 << 9)   /* Low Speed Device Attached */
#define OHCI_PORT_CSC        (1 << 16)  /* Connect Status Change */
#define OHCI_PORT_PESC       (1 << 17)  /* Port Enable Status Change */
#define OHCI_PORT_PSSC       (1 << 18)  /* Port Suspend Status Change */
#define OHCI_PORT_PRSC       (1 << 20)  /* Port Reset Status Change */

/* OHCI Endpoint Descriptor */
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t control;
    uint32_t tail_td;    /* Physical pointer to tail TD */
    uint32_t head_td;    /* Physical pointer to head TD (bits 0-3 are flags) */
    uint32_t next_ed;    /* Physical pointer to next ED */
} ohci_ed_t;

/* OHCI Transfer Descriptor */
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t control;
    uint32_t cbp;        /* Current Buffer Pointer */
    uint32_t next_td;    /* Physical pointer to next TD */
    uint32_t be;         /* Buffer End */
} ohci_td_t;

/* OHCI HCCA (Host Controller Communications Area) */
typedef struct __attribute__((packed, aligned(256))) {
    uint32_t interrupt_table[32];
    uint16_t frame_number;
    uint16_t pad1;
    uint32_t done_head;
    uint8_t  reserved[116];
} ohci_hcca_t;

/* OHCI ED control field bits */
#define OHCI_ED_FA_SHIFT     0   /* Function Address */
#define OHCI_ED_EN_SHIFT     7   /* Endpoint Number */
#define OHCI_ED_DIR_TD       0x00000000  /* Get direction from TD */
#define OHCI_ED_DIR_OUT      0x00000800
#define OHCI_ED_DIR_IN       0x00001000
#define OHCI_ED_SPEED_FULL   0x00000000
#define OHCI_ED_SPEED_LOW    0x00002000
#define OHCI_ED_SKIP         0x00004000
#define OHCI_ED_FORMAT_GEN   0x00000000
#define OHCI_ED_FORMAT_ISO   0x00008000
#define OHCI_ED_MPS_SHIFT    16  /* Max Packet Size */

/* OHCI TD control field bits */
#define OHCI_TD_DP_SETUP     0x00000000
#define OHCI_TD_DP_OUT       0x00080000
#define OHCI_TD_DP_IN        0x00100000
#define OHCI_TD_TOGGLE_0     0x02000000
#define OHCI_TD_TOGGLE_1     0x03000000
#define OHCI_TD_DI_NONE      0x00E00000  /* No interrupt delay */
#define OHCI_TD_CC_MASK      0xF0000000
#define OHCI_TD_CC_NOERROR   0x00000000

typedef struct {
    volatile uint8_t *base;  /* MMIO base address */
    ohci_hcca_t      *hcca;
    uint32_t          hcca_phys;
    ohci_ed_t        *ed_pool;
    uint32_t          ed_pool_phys;
    int               ed_pool_used;
    ohci_td_t        *td_pool;
    uint32_t          td_pool_phys;
    int               td_pool_used;
    bool              initialized;
    int               num_ports;
} ohci_controller_t;

#define MAX_OHCI_CONTROLLERS 4
#define OHCI_ED_POOL_SIZE    16
#define OHCI_TD_POOL_SIZE    64

static ohci_controller_t ohci_controllers[MAX_OHCI_CONTROLLERS];
static int ohci_controller_count = 0;

static bool ohci_init_controller(int pci_idx) {
    if (ohci_controller_count >= MAX_OHCI_CONTROLLERS) return false;
    
    pci_usb_device_t *pci = &usb_controllers[pci_idx];
    ohci_controller_t *hc = &ohci_controllers[ohci_controller_count];
    
    /* OHCI uses MMIO BAR0 */
    uint32_t bar0 = pci->bar[0] & 0xFFFFF000;
    if (bar0 == 0) return false;
    
    hc->base = (volatile uint8_t *)(uintptr_t)bar0;
    
    /* Check revision */
    uint32_t rev = mmio_read32(hc->base + OHCI_REVISION);
    if ((rev & 0xFF) != 0x10) {
        /* Not OHCI 1.0 - might still work */
    }
    
    /* Take ownership from SMM/BIOS if needed */
    uint32_t control = mmio_read32(hc->base + OHCI_CONTROL);
    if (control & OHCI_CTRL_HCFS_MASK) {
        /* Controller is not in reset state - may need to take ownership */
        if ((control & OHCI_CTRL_HCFS_MASK) != OHCI_CTRL_HCFS_OPER) {
            /* Resume first */
            mmio_write32(hc->base + OHCI_CONTROL,
                        (control & ~OHCI_CTRL_HCFS_MASK) | OHCI_CTRL_HCFS_RESUME);
            mdelay(20);
        }
    }
    
    /* Reset controller */
    mmio_write32(hc->base + OHCI_CMDSTATUS, OHCI_CMDSTATUS_HCR);
    mdelay(10);
    
    int timeout = 100;
    while ((mmio_read32(hc->base + OHCI_CMDSTATUS) & OHCI_CMDSTATUS_HCR) && timeout-- > 0) {
        mdelay(1);
    }
    
    /* Allocate HCCA */
    hc->hcca = (ohci_hcca_t *)dma_alloc(sizeof(ohci_hcca_t), 256);
    if (!hc->hcca) return false;
    hc->hcca_phys = virt_to_phys(hc->hcca);
    
    /* Allocate ED pool */
    hc->ed_pool = (ohci_ed_t *)dma_alloc(sizeof(ohci_ed_t) * OHCI_ED_POOL_SIZE, 16);
    if (!hc->ed_pool) return false;
    hc->ed_pool_phys = virt_to_phys(hc->ed_pool);
    hc->ed_pool_used = 0;
    
    /* Allocate TD pool */
    hc->td_pool = (ohci_td_t *)dma_alloc(sizeof(ohci_td_t) * OHCI_TD_POOL_SIZE, 16);
    if (!hc->td_pool) return false;
    hc->td_pool_phys = virt_to_phys(hc->td_pool);
    hc->td_pool_used = 0;
    
    /* Initialize HCCA interrupt table - empty for now */
    for (int i = 0; i < 32; i++) {
        hc->hcca->interrupt_table[i] = 0;
    }
    
    /* Set HCCA base */
    mmio_write32(hc->base + OHCI_HCCA, hc->hcca_phys);
    
    /* Clear all ED head pointers */
    mmio_write32(hc->base + OHCI_CTRL_HEAD_ED, 0);
    mmio_write32(hc->base + OHCI_CTRL_CUR_ED, 0);
    mmio_write32(hc->base + OHCI_BULK_HEAD_ED, 0);
    mmio_write32(hc->base + OHCI_BULK_CUR_ED, 0);
    
    /* Set frame interval */
    uint32_t fminterval = 0xA7782EDF; /* Default: 11999 bit times, FSLargestDataPacket=0x2778 */
    mmio_write32(hc->base + OHCI_FMINTERVAL, fminterval);
    mmio_write32(hc->base + OHCI_PERIODICSTART, 0x00002A2F); /* 90% of frame interval */
    
    /* Clear interrupt status */
    mmio_write32(hc->base + OHCI_INTRSTATUS, 0xFFFFFFFF);
    
    /* Disable all interrupts (we poll) */
    mmio_write32(hc->base + OHCI_INTRDISABLE, 0xFFFFFFFF);
    
    /* Enter operational state */
    mmio_write32(hc->base + OHCI_CONTROL,
                OHCI_CTRL_HCFS_OPER | OHCI_CTRL_PLE | OHCI_CTRL_CLE |
                OHCI_CTRL_BLE | OHCI_CTRL_CBSR_MASK);
    mdelay(10);
    
    /* Get number of downstream ports */
    uint32_t rh_desc_a = mmio_read32(hc->base + OHCI_RH_DESCRIPTORA);
    hc->num_ports = rh_desc_a & 0xFF;
    if (hc->num_ports > 15) hc->num_ports = 15;
    
    /* Power on all ports */
    for (int i = 0; i < hc->num_ports; i++) {
        mmio_write32(hc->base + OHCI_RH_PORT_STATUS + i * 4, OHCI_PORT_PPS);
    }
    mdelay(100); /* Wait for ports to power up */
    
    hc->initialized = true;
    ohci_controller_count++;
    
    return true;
}

/* Allocate an OHCI ED */
static ohci_ed_t *ohci_alloc_ed(ohci_controller_t *hc) {
    if (hc->ed_pool_used >= OHCI_ED_POOL_SIZE) return NULL;
    ohci_ed_t *ed = &hc->ed_pool[hc->ed_pool_used++];
    ed->control = 0;
    ed->tail_td = 0;
    ed->head_td = 0;
    ed->next_ed = 0;
    return ed;
}

/* Allocate an OHCI TD */
static ohci_td_t *ohci_alloc_td(ohci_controller_t *hc) {
    if (hc->td_pool_used >= OHCI_TD_POOL_SIZE) return NULL;
    ohci_td_t *td = &hc->td_pool[hc->td_pool_used++];
    td->control = 0;
    td->cbp = 0;
    td->next_td = 0;
    td->be = 0;
    return td;
}

/* Reset an OHCI port */
static bool ohci_port_reset(ohci_controller_t *hc, int port) {
    volatile uint8_t *port_reg = hc->base + OHCI_RH_PORT_STATUS + port * 4;
    
    uint32_t status = mmio_read32(port_reg);
    if (!(status & OHCI_PORT_CCS)) return false;
    
    /* Reset port */
    mmio_write32(port_reg, OHCI_PORT_PRS);
    
    /* Wait for reset complete */
    int timeout = 100;
    while (timeout-- > 0) {
        mdelay(10);
        status = mmio_read32(port_reg);
        if (status & OHCI_PORT_PRSC) {
            /* Reset complete - clear change bit */
            mmio_write32(port_reg, OHCI_PORT_PRSC);
            mdelay(10);
            return true;
        }
    }
    
    return false;
}

static bool ohci_port_is_low_speed(ohci_controller_t *hc, int port) {
    uint32_t status = mmio_read32(hc->base + OHCI_RH_PORT_STATUS + port * 4);
    return (status & OHCI_PORT_LSDA) != 0;
}

/* Execute a control transfer via OHCI */
static int ohci_control_transfer(ohci_controller_t *hc, uint8_t addr, bool low_speed,
                                  usb_setup_packet_t *setup, void *data, uint16_t data_len) {
    int td_start = hc->td_pool_used;
    int ed_start = hc->ed_pool_used;
    
    /* Allocate ED for control endpoint */
    ohci_ed_t *ed = ohci_alloc_ed(hc);
    if (!ed) return -1;
    
    /* Setup ED */
    uint32_t ed_ctrl = ((uint32_t)addr << OHCI_ED_FA_SHIFT) |
                        (0 << OHCI_ED_EN_SHIFT) |       /* Endpoint 0 */
                        OHCI_ED_DIR_TD |                  /* Direction from TD */
                        ((uint32_t)8 << OHCI_ED_MPS_SHIFT); /* Max packet 8 */
    if (low_speed) ed_ctrl |= OHCI_ED_SPEED_LOW;
    ed->control = ed_ctrl;
    
    /* Allocate SETUP TD */
    ohci_td_t *setup_td = ohci_alloc_td(hc);
    if (!setup_td) return -1;
    
    void *setup_buf = dma_alloc(8, 16);
    if (!setup_buf) return -1;
    uint8_t *s = (uint8_t *)setup;
    uint8_t *d = (uint8_t *)setup_buf;
    for (int i = 0; i < 8; i++) d[i] = s[i];
    
    setup_td->control = OHCI_TD_DP_SETUP | OHCI_TD_TOGGLE_0 | OHCI_TD_DI_NONE;
    setup_td->cbp = virt_to_phys(setup_buf);
    setup_td->be = virt_to_phys(setup_buf) + 7;
    
    ohci_td_t *prev_td = setup_td;
    
    /* DATA TD (if any) */
    void *data_buf = NULL;
    if (data_len > 0 && data) {
        data_buf = dma_alloc(data_len, 16);
        if (!data_buf) return -1;
        
        if (!(setup->bmRequestType & USB_DIR_IN)) {
            uint8_t *src_p = (uint8_t *)data;
            uint8_t *dst_p = (uint8_t *)data_buf;
            for (int i = 0; i < data_len; i++) dst_p[i] = src_p[i];
        }
        
        ohci_td_t *data_td = ohci_alloc_td(hc);
        if (!data_td) return -1;
        
        data_td->control = ((setup->bmRequestType & USB_DIR_IN) ? OHCI_TD_DP_IN : OHCI_TD_DP_OUT) |
                            OHCI_TD_TOGGLE_1 | OHCI_TD_DI_NONE;
        data_td->cbp = virt_to_phys(data_buf);
        data_td->be = virt_to_phys(data_buf) + data_len - 1;
        
        prev_td->next_td = virt_to_phys(data_td);
        prev_td = data_td;
    }
    
    /* STATUS TD */
    ohci_td_t *status_td = ohci_alloc_td(hc);
    if (!status_td) return -1;
    
    status_td->control = ((setup->bmRequestType & USB_DIR_IN) ? OHCI_TD_DP_OUT : OHCI_TD_DP_IN) |
                          OHCI_TD_TOGGLE_1 | OHCI_TD_DI_NONE;
    status_td->cbp = 0;
    status_td->be = 0;
    
    prev_td->next_td = virt_to_phys(status_td);
    
    /* Dummy TD for tail */
    ohci_td_t *dummy_td = ohci_alloc_td(hc);
    if (!dummy_td) return -1;
    status_td->next_td = virt_to_phys(dummy_td);
    
    /* Set ED head and tail */
    ed->head_td = virt_to_phys(setup_td);
    ed->tail_td = virt_to_phys(dummy_td);
    ed->next_ed = 0;
    
    /* Insert ED into control list */
    mmio_write32(hc->base + OHCI_CTRL_HEAD_ED, virt_to_phys(ed));
    mmio_write32(hc->base + OHCI_CTRL_CUR_ED, 0);
    
    /* Tell HC to process control list */
    mmio_write32(hc->base + OHCI_CMDSTATUS, OHCI_CMDSTATUS_CLF);
    
    /* Wait for completion */
    int timeout_ms = 5000;
    while (timeout_ms-- > 0) {
        memory_barrier();
        uint32_t head = ed->head_td & 0xFFFFFFF0;
        uint32_t tail = ed->tail_td & 0xFFFFFFF0;
        if (head == tail) break; /* All TDs consumed */
        if (ed->head_td & 0x01) break; /* Halted */
        mdelay(1);
    }
    
    /* Check for errors */
    int result = 0;
    uint32_t cc = (status_td->control & OHCI_TD_CC_MASK) >> 28;
    if (ed->head_td & 0x01) {
        result = -3; /* Halted = error */
    } else if (cc != 0 && cc != 0xF) {
        result = -3;
    } else if (data_buf && (setup->bmRequestType & USB_DIR_IN)) {
        uint8_t *src_p = (uint8_t *)data_buf;
        uint8_t *dst_p = (uint8_t *)data;
        for (int i = 0; i < data_len; i++) dst_p[i] = src_p[i];
        result = data_len;
    }
    
    /* Remove ED from control list */
    mmio_write32(hc->base + OHCI_CTRL_HEAD_ED, 0);
    
    /* Free resources */
    hc->td_pool_used = td_start;
    hc->ed_pool_used = ed_start;
    
    return result;
}

/* Setup interrupt polling for OHCI mouse */
static void ohci_setup_interrupt_polling(ohci_controller_t *hc, usb_mouse_device_t *dev) {
    ohci_ed_t *ed = ohci_alloc_ed(hc);
    if (!ed) return;
    
    dev->transfer_buffer = dma_alloc(dev->max_packet_size, 16);
    if (!dev->transfer_buffer) return;
    dev->transfer_phys = virt_to_phys(dev->transfer_buffer);
    
    /* Allocate two TDs: one active, one dummy tail */
    ohci_td_t *td = ohci_alloc_td(hc);
    ohci_td_t *dummy = ohci_alloc_td(hc);
    if (!td || !dummy) return;
    
    td->control = OHCI_TD_DP_IN | OHCI_TD_DI_NONE;
    if (dev->toggle) td->control |= OHCI_TD_TOGGLE_1;
    else td->control |= OHCI_TD_TOGGLE_0;
    td->cbp = dev->transfer_phys;
    td->be = dev->transfer_phys + dev->max_packet_size - 1;
    td->next_td = virt_to_phys(dummy);
    
    /* Setup ED */
    uint32_t ed_ctrl = ((uint32_t)dev->address << OHCI_ED_FA_SHIFT) |
                        ((uint32_t)dev->endpoint << OHCI_ED_EN_SHIFT) |
                        OHCI_ED_DIR_IN |
                        ((uint32_t)dev->max_packet_size << OHCI_ED_MPS_SHIFT);
    /* Check if low speed - stored in device struct implicitly */
    ed->control = ed_ctrl;
    ed->head_td = virt_to_phys(td);
    ed->tail_td = virt_to_phys(dummy);
    ed->next_ed = 0;
    
    dev->td_pool = td;
    dev->td_pool_phys = virt_to_phys(td);
    
    /* Insert ED into interrupt table at appropriate intervals */
    uint32_t ed_phys = virt_to_phys(ed);
    int interval = dev->interval;
    if (interval < 1) interval = 1;
    if (interval > 32) interval = 32;
    
    for (int i = 0; i < 32; i += interval) {
        if (hc->hcca->interrupt_table[i] == 0) {
            hc->hcca->interrupt_table[i] = ed_phys;
        } else {
            /* Chain to existing */
            ed->next_ed = hc->hcca->interrupt_table[i];
            hc->hcca->interrupt_table[i] = ed_phys;
        }
    }
    memory_barrier();
}

/* Poll OHCI interrupt transfer */
static void ohci_poll_mouse(ohci_controller_t *hc, usb_mouse_device_t *dev) {
    if (!dev->td_pool || !dev->transfer_buffer) return;
    
    ohci_td_t *td = (ohci_td_t *)dev->td_pool;
    memory_barrier();
    
    uint32_t cc = (td->control & OHCI_TD_CC_MASK) >> 28;
    
    /* Check if TD has been retired (CC != 0xF means processed, 0 = no error) */
    if (cc == 0xF) return; /* Not yet processed */
    
    if (cc == 0) {
        /* Success - parse mouse report */
        uint8_t *report = (uint8_t *)dev->transfer_buffer;
        int actual_len = dev->max_packet_size; /* OHCI doesn't easily give actual length without checking CBP */
        
        if (actual_len >= 3) {
            uint8_t btn = 0;
            if (report[0] & 0x01) btn |= MOUSE_BUTTON_LEFT;
            if (report[0] & 0x02) btn |= MOUSE_BUTTON_RIGHT;
            if (report[0] & 0x04) btn |= MOUSE_BUTTON_MIDDLE;
            if (report[0] & 0x08) btn |= MOUSE_BUTTON_4;
            if (report[0] & 0x10) btn |= MOUSE_BUTTON_5;
            
            int dx = mouse_accelerate((int8_t)report[1]);
            int dy = mouse_accelerate((int8_t)report[2]);
            int dz = (actual_len >= 4) ? (int8_t)report[3] : 0;
            
            mouse_apply_movement(dx, dy);
            
            mouse_clamp_position();
            mouse.z_accum += dz;
            mouse_update_buttons(btn);
            
            mouse_event_t evt = {
                .x = mouse.x, .y = mouse.y,
                .dx = dx, .dy = dy, .dz = dz,
                .buttons = mouse.buttons,
                .absolute = false,
                .timestamp = rdtsc()
            };
            mouse_queue_event(&evt);
            mouse.total_packets++;
        }
    }
    
    /* Resubmit: re-initialize TD */
    dev->toggle ^= 1;
    td->control = OHCI_TD_DP_IN | OHCI_TD_DI_NONE;
    if (dev->toggle) td->control |= OHCI_TD_TOGGLE_1;
    else td->control |= OHCI_TD_TOGGLE_0;
    td->cbp = dev->transfer_phys;
    td->be = dev->transfer_phys + dev->max_packet_size - 1;
    memory_barrier();
}

/* Section 15: EHCI (Enhanced Host Controller Interface) */

/* EHCI capability registers */
#define EHCI_CAP_CAPLENGTH     0x00
#define EHCI_CAP_HCIVERSION    0x02
#define EHCI_CAP_HCSPARAMS     0x04
#define EHCI_CAP_HCCPARAMS     0x08

/* EHCI operational registers (offset from cap_length) */
#define EHCI_OP_USBCMD         0x00
#define EHCI_OP_USBSTS         0x04
#define EHCI_OP_USBINTR        0x08
#define EHCI_OP_FRINDEX        0x0C
#define EHCI_OP_CTRLDSSEGMENT  0x10
#define EHCI_OP_PERIODICBASE   0x14
#define EHCI_OP_ASYNCLISTADDR  0x18
#define EHCI_OP_CONFIGFLAG     0x40
#define EHCI_OP_PORTSC         0x44  /* + port*4 */

/* EHCI command bits */
#define EHCI_CMD_RS            (1 << 0)
#define EHCI_CMD_HCRESET       (1 << 1)
#define EHCI_CMD_PSE           (1 << 4)  /* Periodic Schedule Enable */
#define EHCI_CMD_ASE           (1 << 5)  /* Async Schedule Enable */
#define EHCI_CMD_ITC_MASK      (0xFF << 16) /* Interrupt Threshold */

/* EHCI status bits */
#define EHCI_STS_USBINT        (1 << 0)
#define EHCI_STS_ERROR         (1 << 1)
#define EHCI_STS_PCD           (1 << 2)  /* Port Change Detect */
#define EHCI_STS_FLR           (1 << 3)  /* Frame List Rollover */
#define EHCI_STS_HSE           (1 << 4)  /* Host System Error */
#define EHCI_STS_IAA           (1 << 5)  /* Interrupt on Async Advance */
#define EHCI_STS_HALT          (1 << 12)
#define EHCI_STS_RECL          (1 << 13)
#define EHCI_STS_PSS           (1 << 14)
#define EHCI_STS_ASS           (1 << 15)

/* EHCI port status bits */
#define EHCI_PORT_CCS          (1 << 0)
#define EHCI_PORT_CSC          (1 << 1)
#define EHCI_PORT_PE           (1 << 2)
#define EHCI_PORT_PEC          (1 << 3)
#define EHCI_PORT_OCA          (1 << 4)
#define EHCI_PORT_OCC          (1 << 5)
#define EHCI_PORT_FPR          (1 << 6)
#define EHCI_PORT_SUSPEND      (1 << 7)
#define EHCI_PORT_RESET        (1 << 8)
#define EHCI_PORT_LS_MASK      (3 << 10)
#define EHCI_PORT_LS_SE0       (0 << 10)
#define EHCI_PORT_LS_JSTATE    (1 << 10)
#define EHCI_PORT_LS_KSTATE    (2 << 10)
#define EHCI_PORT_PP           (1 << 12)
#define EHCI_PORT_OWNER        (1 << 13)

/* EHCI Queue Head */
typedef struct __attribute__((packed, aligned(32))) {
    uint32_t horizontal_link;
    uint32_t endpoint_chars;
    uint32_t endpoint_caps;
    uint32_t current_td;
    /* Overlay area (transfer state) */
    uint32_t next_td;
    uint32_t alt_next_td;
    uint32_t token;
    uint32_t buffer[5];
    /* Extended buffer pointers (64-bit) - we only use 32-bit */
    uint32_t ext_buffer[5];
    /* Software fields */
    uint32_t sw_pad[4];
} ehci_qh_t;

/* EHCI Queue Transfer Descriptor */
typedef struct __attribute__((packed, aligned(32))) {
    uint32_t next_td;
    uint32_t alt_next_td;
    uint32_t token;
    uint32_t buffer[5];
    /* Extended buffers */
    uint32_t ext_buffer[5];
    /* Software fields */
    uint32_t sw_pad[2];
} ehci_qtd_t;

/* EHCI QTD token bits */
#define EHCI_QTD_STATUS_ACTIVE    (1 << 7)
#define EHCI_QTD_STATUS_HALTED    (1 << 6)
#define EHCI_QTD_STATUS_BUFERR    (1 << 5)
#define EHCI_QTD_STATUS_BABBLE    (1 << 4)
#define EHCI_QTD_STATUS_XACTERR   (1 << 3)
#define EHCI_QTD_STATUS_MISSED    (1 << 2)
#define EHCI_QTD_STATUS_SPLIT     (1 << 1)
#define EHCI_QTD_STATUS_PING      (1 << 0)
#define EHCI_QTD_PID_OUT          (0 << 8)
#define EHCI_QTD_PID_IN           (1 << 8)
#define EHCI_QTD_PID_SETUP        (2 << 8)
#define EHCI_QTD_CERR_SHIFT       10
#define EHCI_QTD_IOC              (1 << 15)
#define EHCI_QTD_TOGGLE           (1 << 31)
#define EHCI_QTD_TOTAL_SHIFT      16

#define EHCI_QH_LINK_TERMINATE    0x01
#define EHCI_QH_LINK_QH          0x02
#define EHCI_QTD_LINK_TERMINATE   0x01

typedef struct {
    volatile uint8_t *base;
    volatile uint8_t *op_base;    /* Operational registers base */
    uint8_t           cap_length;
    int               num_ports;
    uint32_t         *periodic_list;
    uint32_t          periodic_list_phys;
    ehci_qh_t        *async_qh;
    uint32_t          async_qh_phys;
    ehci_qh_t        *qh_pool;
    uint32_t          qh_pool_phys;
    int               qh_pool_used;
    ehci_qtd_t       *qtd_pool;
    uint32_t          qtd_pool_phys;
    int               qtd_pool_used;
    bool              initialized;
    bool              has_64bit;
} ehci_controller_t;

#define MAX_EHCI_CONTROLLERS 4
#define EHCI_QH_POOL_SIZE    16
#define EHCI_QTD_POOL_SIZE   64

static ehci_controller_t ehci_controllers[MAX_EHCI_CONTROLLERS];
static int ehci_controller_count = 0;

static bool ehci_init_controller(int pci_idx) {
    if (ehci_controller_count >= MAX_EHCI_CONTROLLERS) return false;
    
    pci_usb_device_t *pci = &usb_controllers[pci_idx];
    ehci_controller_t *hc = &ehci_controllers[ehci_controller_count];
    
    /* EHCI uses MMIO BAR0 */
    uint32_t bar0 = pci->bar[0] & 0xFFFFF000;
    if (bar0 == 0) return false;
    
    hc->base = (volatile uint8_t *)(uintptr_t)bar0;
    hc->cap_length = mmio_read8(hc->base + EHCI_CAP_CAPLENGTH);
    hc->op_base = hc->base + hc->cap_length;
    
    /* Read capabilities */
    uint32_t hcsparams = mmio_read32(hc->base + EHCI_CAP_HCSPARAMS);
    hc->num_ports = hcsparams & 0x0F;
    if (hc->num_ports > 15) hc->num_ports = 15;
    
    uint32_t hccparams = mmio_read32(hc->base + EHCI_CAP_HCCPARAMS);
    hc->has_64bit = (hccparams & 0x01) != 0;
    
    /* Take ownership from BIOS (EECP) */
    uint8_t eecp = (hccparams >> 8) & 0xFF;
    if (eecp >= 0x40) {
        uint32_t legsup = pci_config_read32(pci->bus, pci->dev, pci->func, eecp);
        if (legsup & (1 << 16)) {
            /* BIOS owns it, request ownership */
            pci_config_write32(pci->bus, pci->dev, pci->func, eecp,
                              legsup | (1 << 24)); /* Set OS ownership */
            
            /* Wait for BIOS to release */
            int timeout = 100;
            while (timeout-- > 0) {
                legsup = pci_config_read32(pci->bus, pci->dev, pci->func, eecp);
                if (!(legsup & (1 << 16))) break;
                mdelay(10);
            }
        }
        /* Disable SMI */
        if (eecp + 4 < 256) {
            pci_config_write32(pci->bus, pci->dev, pci->func, eecp + 4, 0);
        }
    }
    
    /* Stop and reset */
    uint32_t cmd = mmio_read32(hc->op_base + EHCI_OP_USBCMD);
    cmd &= ~(EHCI_CMD_RS | EHCI_CMD_ASE | EHCI_CMD_PSE);
    mmio_write32(hc->op_base + EHCI_OP_USBCMD, cmd);
    mdelay(2);
    
    /* Wait for halt */
    int timeout = 100;
    while (timeout-- > 0) {
        if (mmio_read32(hc->op_base + EHCI_OP_USBSTS) & EHCI_STS_HALT) break;
        mdelay(1);
    }
    
    /* Reset */
    mmio_write32(hc->op_base + EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    mdelay(50);
    timeout = 100;
    while (timeout-- > 0) {
        if (!(mmio_read32(hc->op_base + EHCI_OP_USBCMD) & EHCI_CMD_HCRESET)) break;
        mdelay(1);
    }
    
    /* Allocate periodic frame list (4KB) */
    hc->periodic_list = (uint32_t *)dma_alloc(4096, 4096);
    if (!hc->periodic_list) return false;
    hc->periodic_list_phys = virt_to_phys(hc->periodic_list);
    
    /* Initialize periodic list - all terminate */
    for (int i = 0; i < 1024; i++) {
        hc->periodic_list[i] = EHCI_QH_LINK_TERMINATE;
    }
    
    /* Allocate async QH (circular list of one QH) */
    hc->async_qh = (ehci_qh_t *)dma_alloc(sizeof(ehci_qh_t), 32);
    if (!hc->async_qh) return false;
    hc->async_qh_phys = virt_to_phys(hc->async_qh);
    
    /* Self-referencing async QH */
    hc->async_qh->horizontal_link = hc->async_qh_phys | EHCI_QH_LINK_QH;
    hc->async_qh->endpoint_chars = (1 << 15) | (64 << 16); /* H-bit, max packet 64 */
    hc->async_qh->endpoint_caps = (1 << 30); /* Mult=1 */
    hc->async_qh->next_td = EHCI_QTD_LINK_TERMINATE;
    hc->async_qh->alt_next_td = EHCI_QTD_LINK_TERMINATE;
    hc->async_qh->token = 0;
    
    /* Allocate QH pool */
    hc->qh_pool = (ehci_qh_t *)dma_alloc(sizeof(ehci_qh_t) * EHCI_QH_POOL_SIZE, 32);
    if (!hc->qh_pool) return false;
    hc->qh_pool_phys = virt_to_phys(hc->qh_pool);
    hc->qh_pool_used = 0;
    
    /* Allocate QTD pool */
    hc->qtd_pool = (ehci_qtd_t *)dma_alloc(sizeof(ehci_qtd_t) * EHCI_QTD_POOL_SIZE, 32);
    if (!hc->qtd_pool) return false;
    hc->qtd_pool_phys = virt_to_phys(hc->qtd_pool);
    hc->qtd_pool_used = 0;
    
    /* Set segment selector to 0 (32-bit addresses) */
    if (hc->has_64bit) {
        mmio_write32(hc->op_base + EHCI_OP_CTRLDSSEGMENT, 0);
    }
    
    /* Set periodic and async list addresses */
    mmio_write32(hc->op_base + EHCI_OP_PERIODICBASE, hc->periodic_list_phys);
    mmio_write32(hc->op_base + EHCI_OP_ASYNCLISTADDR, hc->async_qh_phys);
    
    /* Clear status */
    mmio_write32(hc->op_base + EHCI_OP_USBSTS, 0x3F);
    
    /* Disable interrupts (we poll) */
    mmio_write32(hc->op_base + EHCI_OP_USBINTR, 0);
    
    /* Start controller with periodic and async schedules */
    cmd = EHCI_CMD_RS | EHCI_CMD_ASE | EHCI_CMD_PSE | (8 << 16); /* ITC=8 microframes */
    /* Frame list size = 1024 (bits 3:2 = 00) */
    mmio_write32(hc->op_base + EHCI_OP_USBCMD, cmd);
    mdelay(10);
    
    /* Set CF flag to route all ports to EHCI */
    mmio_write32(hc->op_base + EHCI_OP_CONFIGFLAG, 1);
    mdelay(50);
    
    /* Power on ports */
    for (int i = 0; i < hc->num_ports; i++) {
        uint32_t portsc = mmio_read32(hc->op_base + EHCI_OP_PORTSC + i * 4);
        portsc |= EHCI_PORT_PP;
        mmio_write32(hc->op_base + EHCI_OP_PORTSC + i * 4, portsc);
    }
    mdelay(100);
    
    hc->initialized = true;
    ehci_controller_count++;
    
    return true;
}

/* Reset an EHCI port */
static bool ehci_port_reset(ehci_controller_t *hc, int port) {
    volatile uint8_t *portsc = hc->op_base + EHCI_OP_PORTSC + port * 4;
    
    uint32_t status = mmio_read32(portsc);
    if (!(status & EHCI_PORT_CCS)) return false;
    
    /* Check line status - if K-state (low speed), release to companion */
    uint32_t ls = (status & EHCI_PORT_LS_MASK);
    if (ls == EHCI_PORT_LS_KSTATE) {
        /* Low-speed device - release to companion controller */
        mmio_write32(portsc, status | EHCI_PORT_OWNER);
        return false;
    }
    
    /* Disable port before reset */
    status &= ~EHCI_PORT_PE;
    mmio_write32(portsc, status);
    mdelay(10);
    
    /* Assert reset */
    status = mmio_read32(portsc);
    status |= EHCI_PORT_RESET;
    status &= ~EHCI_PORT_PE;
    mmio_write32(portsc, status);
    mdelay(50);
    
    /* De-assert reset */
    status = mmio_read32(portsc);
    status &= ~EHCI_PORT_RESET;
    mmio_write32(portsc, status);
    mdelay(20);
    
    /* Wait for reset to complete and port to enable */
    int timeout = 100;
    while (timeout-- > 0) {
        status = mmio_read32(portsc);
        if (!(status & EHCI_PORT_RESET)) {
            if (status & EHCI_PORT_PE) return true;
            if (!(status & EHCI_PORT_CCS)) return false;
        }
        mdelay(5);
    }
    
    /* If not enabled, device is not high-speed, release to companion */
    status = mmio_read32(portsc);
    if (!(status & EHCI_PORT_PE)) {
        mmio_write32(portsc, status | EHCI_PORT_OWNER);
        return false;
    }
    
    return true;
}

/* Execute control transfer via EHCI */
static int ehci_control_transfer(ehci_controller_t *hc, uint8_t addr,
                                  usb_setup_packet_t *setup, void *data, uint16_t data_len) {
    int qtd_start = hc->qtd_pool_used;
    
    /* Allocate QTDs */
    ehci_qtd_t *setup_qtd = &hc->qtd_pool[hc->qtd_pool_used++];
    
    /* Prepare setup data */
    void *setup_buf = dma_alloc(8, 16);
    if (!setup_buf) return -1;
    uint8_t *s = (uint8_t *)setup;
    uint8_t *d2 = (uint8_t *)setup_buf;
    for (int i = 0; i < 8; i++) d2[i] = s[i];
    
    /* Setup QTD */
    setup_qtd->alt_next_td = EHCI_QTD_LINK_TERMINATE;
    setup_qtd->token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_PID_SETUP |
                       (3 << EHCI_QTD_CERR_SHIFT) | (8 << EHCI_QTD_TOTAL_SHIFT);
    setup_qtd->buffer[0] = virt_to_phys(setup_buf);
    for (int i = 1; i < 5; i++) setup_qtd->buffer[i] = 0;
    for (int i = 0; i < 5; i++) setup_qtd->ext_buffer[i] = 0;
    
    ehci_qtd_t *prev_qtd = setup_qtd;
    
    /* Data QTD */
    void *data_buf = NULL;
    if (data_len > 0 && data) {
        data_buf = dma_alloc(data_len, 16);
        if (!data_buf) return -1;
        
        if (!(setup->bmRequestType & USB_DIR_IN)) {
            uint8_t *src_p = (uint8_t *)data;
            uint8_t *dst_p = (uint8_t *)data_buf;
            for (int i = 0; i < data_len; i++) dst_p[i] = src_p[i];
        }
        
        ehci_qtd_t *data_qtd = &hc->qtd_pool[hc->qtd_pool_used++];
        data_qtd->alt_next_td = EHCI_QTD_LINK_TERMINATE;
        data_qtd->token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_TOGGLE |
                          (3 << EHCI_QTD_CERR_SHIFT) |
                          ((uint32_t)data_len << EHCI_QTD_TOTAL_SHIFT) |
                          ((setup->bmRequestType & USB_DIR_IN) ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT);
        data_qtd->buffer[0] = virt_to_phys(data_buf);
        for (int i = 1; i < 5; i++) data_qtd->buffer[i] = 0;
        for (int i = 0; i < 5; i++) data_qtd->ext_buffer[i] = 0;
        
        prev_qtd->next_td = virt_to_phys(data_qtd);
        prev_qtd = data_qtd;
    }
    
    /* Status QTD */
    ehci_qtd_t *status_qtd = &hc->qtd_pool[hc->qtd_pool_used++];
    status_qtd->next_td = EHCI_QTD_LINK_TERMINATE;
    status_qtd->alt_next_td = EHCI_QTD_LINK_TERMINATE;
    status_qtd->token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_IOC | EHCI_QTD_TOGGLE |
                        (3 << EHCI_QTD_CERR_SHIFT) |
                        ((setup->bmRequestType & USB_DIR_IN) ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN);
    for (int i = 0; i < 5; i++) { status_qtd->buffer[i] = 0; status_qtd->ext_buffer[i] = 0; }
    
    prev_qtd->next_td = virt_to_phys(status_qtd);
    
    /* Create a temporary QH */
    ehci_qh_t *qh = &hc->qh_pool[hc->qh_pool_used++];
    
    /* Insert into async list right after the async_qh */
    qh->horizontal_link = hc->async_qh->horizontal_link;
    qh->endpoint_chars = ((uint32_t)addr) | (0 << 8) | /* Endpoint 0 */
                          (2 << 12) | /* EPS: High Speed */
                          (64 << 16) | /* Max Packet Size */
                          (1 << 14);   /* DTC: use toggle from QTD */
    qh->endpoint_caps = (1 << 30); /* Mult=1, hub stuff=0 */
    qh->current_td = 0;
    qh->next_td = virt_to_phys(setup_qtd);
    qh->alt_next_td = EHCI_QTD_LINK_TERMINATE;
    qh->token = 0;
    for (int i = 0; i < 5; i++) { qh->buffer[i] = 0; qh->ext_buffer[i] = 0; }
    
    hc->async_qh->horizontal_link = virt_to_phys(qh) | EHCI_QH_LINK_QH;
    memory_barrier();
    
    /* Wait for transfer completion */
    int timeout_ms = 5000;
    while (timeout_ms-- > 0) {
        memory_barrier();
        if (!(status_qtd->token & EHCI_QTD_STATUS_ACTIVE)) break;
        if (status_qtd->token & EHCI_QTD_STATUS_HALTED) break;
        mdelay(1);
    }
    
    /* Remove QH from async list */
    hc->async_qh->horizontal_link = hc->async_qh_phys | EHCI_QH_LINK_QH;
    memory_barrier();
    mdelay(2);
    
    /* Check result */
    int result = 0;
    if (status_qtd->token & EHCI_QTD_STATUS_ACTIVE) {
        result = -2;
    } else if (status_qtd->token & EHCI_QTD_STATUS_HALTED) {
        result = -3;
    } else if (data_buf && (setup->bmRequestType & USB_DIR_IN)) {
        uint8_t *src_p = (uint8_t *)data_buf;
        uint8_t *dst_p = (uint8_t *)data;
        for (int i = 0; i < data_len; i++) dst_p[i] = src_p[i];
        result = data_len;
    }
    
    hc->qtd_pool_used = qtd_start;
    hc->qh_pool_used--;
    
    return result;
}

/* Setup EHCI interrupt polling for mouse */
static void ehci_setup_interrupt_polling(ehci_controller_t *hc, usb_mouse_device_t *dev) {
    dev->transfer_buffer = dma_alloc(dev->max_packet_size, 16);
    if (!dev->transfer_buffer) return;
    dev->transfer_phys = virt_to_phys(dev->transfer_buffer);
    
    /* Allocate QH for interrupt endpoint */
    ehci_qh_t *qh = &hc->qh_pool[hc->qh_pool_used++];
    
    /* Allocate QTD */
    ehci_qtd_t *qtd = &hc->qtd_pool[hc->qtd_pool_used++];
    
    qtd->next_td = EHCI_QTD_LINK_TERMINATE;
    qtd->alt_next_td = EHCI_QTD_LINK_TERMINATE;
    qtd->token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_PID_IN | EHCI_QTD_IOC |
                 (3 << EHCI_QTD_CERR_SHIFT) |
                 ((uint32_t)dev->max_packet_size << EHCI_QTD_TOTAL_SHIFT);
    if (dev->toggle) qtd->token |= EHCI_QTD_TOGGLE;
    qtd->buffer[0] = dev->transfer_phys;
    for (int i = 1; i < 5; i++) qtd->buffer[i] = 0;
    for (int i = 0; i < 5; i++) qtd->ext_buffer[i] = 0;
    
    dev->td_pool = qtd;
    dev->td_pool_phys = virt_to_phys(qtd);
    
    /* Setup QH */
    qh->endpoint_chars = ((uint32_t)dev->address) |
                          ((uint32_t)dev->endpoint << 8) |
                          (2 << 12) | /* High Speed */
                          ((uint32_t)dev->max_packet_size << 16) |
                          (1 << 14); /* DTC */
    qh->endpoint_caps = (1 << 30) | /* Mult=1 */
                         (0x1C << 0); /* S-mask for periodic scheduling */
    qh->current_td = 0;
    qh->next_td = virt_to_phys(qtd);
    qh->alt_next_td = EHCI_QTD_LINK_TERMINATE;
    qh->token = 0;
    for (int i = 0; i < 5; i++) { qh->buffer[i] = 0; qh->ext_buffer[i] = 0; }
    
    /* Insert QH into periodic schedule */
    uint32_t qh_phys = virt_to_phys(qh);
    int interval = dev->interval;
    if (interval < 1) interval = 1;
    if (interval > 128) interval = 128;
    
    for (int i = 0; i < 1024; i += interval) {
        uint32_t old = hc->periodic_list[i];
        qh->horizontal_link = old;
        hc->periodic_list[i] = qh_phys | EHCI_QH_LINK_QH;
    }
    memory_barrier();
}

/* Poll EHCI interrupt transfer */
static void ehci_poll_mouse(ehci_controller_t *hc, usb_mouse_device_t *dev) {
    if (!dev->td_pool || !dev->transfer_buffer) return;
    
    ehci_qtd_t *qtd = (ehci_qtd_t *)dev->td_pool;
    memory_barrier();
    
    if (qtd->token & EHCI_QTD_STATUS_ACTIVE) return;
    
    if (!(qtd->token & EHCI_QTD_STATUS_HALTED)) {
        int total_bytes = (qtd->token >> EHCI_QTD_TOTAL_SHIFT) & 0x7FFF;
        int actual_len = dev->max_packet_size - total_bytes;
        
        if (actual_len >= 3) {
            uint8_t *report = (uint8_t *)dev->transfer_buffer;
            
            uint8_t btn = 0;
            if (report[0] & 0x01) btn |= MOUSE_BUTTON_LEFT;
            if (report[0] & 0x02) btn |= MOUSE_BUTTON_RIGHT;
            if (report[0] & 0x04) btn |= MOUSE_BUTTON_MIDDLE;
            if (report[0] & 0x08) btn |= MOUSE_BUTTON_4;
            if (report[0] & 0x10) btn |= MOUSE_BUTTON_5;
            
            int dx = mouse_accelerate((int8_t)report[1]);
            int dy = mouse_accelerate((int8_t)report[2]);
            int dz = (actual_len >= 4) ? (int8_t)report[3] : 0;
            
            mouse_apply_movement(dx, dy);
            
            mouse_clamp_position();
            mouse.z_accum += dz;
            mouse_update_buttons(btn);
            
            mouse_event_t evt = {
                .x = mouse.x, .y = mouse.y,
                .dx = dx, .dy = dy, .dz = dz,
                .buttons = mouse.buttons,
                .absolute = false,
                .timestamp = rdtsc()
            };
            mouse_queue_event(&evt);
            mouse.total_packets++;
        }
    }
    
    /* Resubmit */
    dev->toggle ^= 1;
    qtd->token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_PID_IN | EHCI_QTD_IOC |
                 (3 << EHCI_QTD_CERR_SHIFT) |
                 ((uint32_t)dev->max_packet_size << EHCI_QTD_TOTAL_SHIFT);
    if (dev->toggle) qtd->token |= EHCI_QTD_TOGGLE;
    qtd->buffer[0] = dev->transfer_phys;
    memory_barrier();
}

/* Section 16: xHCI (Extensible Host Controller Interface) */

/* xHCI capability registers */
#define XHCI_CAP_CAPLENGTH     0x00
#define XHCI_CAP_HCIVERSION    0x02
#define XHCI_CAP_HCSPARAMS1    0x04
#define XHCI_CAP_HCSPARAMS2    0x08
#define XHCI_CAP_HCSPARAMS3    0x0C
#define XHCI_CAP_HCCPARAMS1    0x10
#define XHCI_CAP_DBOFF         0x14
#define XHCI_CAP_RTSOFF        0x18
#define XHCI_CAP_HCCPARAMS2    0x1C

/* xHCI operational registers */
#define XHCI_OP_USBCMD        0x00
#define XHCI_OP_USBSTS        0x04
#define XHCI_OP_PAGESIZE      0x08
#define XHCI_OP_DNCTRL        0x14
#define XHCI_OP_CRCR          0x18  /* 64-bit */
#define XHCI_OP_DCBAAP        0x30  /* 64-bit */
#define XHCI_OP_CONFIG        0x38

/* xHCI port register set (offset within operational regs) */
#define XHCI_OP_PORTS_BASE     0x400
#define XHCI_PORT_PORTSC       0x00
#define XHCI_PORT_PORTPMSC     0x04
#define XHCI_PORT_PORTLI       0x08
#define XHCI_PORT_PORTHLPMC    0x0C

/* xHCI command bits */
#define XHCI_CMD_RS            (1 << 0)
#define XHCI_CMD_HCRST         (1 << 1)
#define XHCI_CMD_INTE          (1 << 2)
#define XHCI_CMD_HSEE          (1 << 3)

/* xHCI status bits */
#define XHCI_STS_HCH           (1 << 0)
#define XHCI_STS_HSE           (1 << 2)
#define XHCI_STS_EINT          (1 << 3)
#define XHCI_STS_PCD           (1 << 4)
#define XHCI_STS_CNR           (1 << 11)

/* xHCI port status bits */
#define XHCI_PORT_CCS          (1 << 0)
#define XHCI_PORT_PED          (1 << 1)
#define XHCI_PORT_OCA          (1 << 3)
#define XHCI_PORT_PR           (1 << 4)
#define XHCI_PORT_PLS_SHIFT    5
#define XHCI_PORT_PLS_MASK     (0xF << 5)
#define XHCI_PORT_PP           (1 << 9)
#define XHCI_PORT_SPEED_SHIFT  10
#define XHCI_PORT_SPEED_MASK   (0xF << 10)
#define XHCI_PORT_CSC          (1 << 17)
#define XHCI_PORT_PRC          (1 << 21)
#define XHCI_PORT_WRC          (1 << 19)

/* xHCI TRB types */
#define XHCI_TRB_NORMAL         1
#define XHCI_TRB_SETUP          2
#define XHCI_TRB_DATA           3
#define XHCI_TRB_STATUS         4
#define XHCI_TRB_LINK           6
#define XHCI_TRB_EVENT_DATA     7
#define XHCI_TRB_NOOP           8
#define XHCI_TRB_ENABLE_SLOT    9
#define XHCI_TRB_DISABLE_SLOT   10
#define XHCI_TRB_ADDRESS_DEV    11
#define XHCI_TRB_CONFIG_EP      12
#define XHCI_TRB_EVAL_CTX       13
#define XHCI_TRB_RESET_EP       14
#define XHCI_TRB_STOP_EP        15
#define XHCI_TRB_SET_TR_DEQUEUE 16
#define XHCI_TRB_RESET_DEV      17
#define XHCI_TRB_NOOP_CMD       23
#define XHCI_TRB_TRANSFER_EVT   32
#define XHCI_TRB_CMD_COMPLETE   33
#define XHCI_TRB_PORT_STATUS    34

/* xHCI TRB */
typedef struct __attribute__((packed, aligned(16))) {
    uint32_t param_lo;
    uint32_t param_hi;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

/* xHCI Slot Context */
typedef struct __attribute__((packed)) {
    uint32_t field1;   /* Route String, Speed, etc */
    uint32_t field2;   /* Max Exit Latency, Root Hub Port, Num Ports */
    uint32_t field3;   /* TT Hub Slot, TT Port, etc */
    uint32_t field4;   /* Slot State, Device Address */
    uint32_t reserved[4];
} xhci_slot_ctx_t;

/* xHCI Endpoint Context */
typedef struct __attribute__((packed)) {
    uint32_t field1;   /* EP State, Mult, MaxPStreams, etc */
    uint32_t field2;   /* Max Packet Size, Max Burst, etc */
    uint32_t tr_dequeue_lo;
    uint32_t tr_dequeue_hi;
    uint32_t field5;   /* Average TRB Length, Max ESIT Payload */
    uint32_t reserved[3];
} xhci_ep_ctx_t;

/* xHCI Input Control Context */
typedef struct __attribute__((packed)) {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[5];
    uint32_t config_value;
    /* Followed by slot context + endpoint contexts */
} xhci_input_control_ctx_t;

#define XHCI_MAX_SLOTS       256
#define XHCI_MAX_EPS         31
#define XHCI_CMD_RING_SIZE   64
#define XHCI_EVT_RING_SIZE   64
#define XHCI_XFER_RING_SIZE  64

typedef struct {
    volatile uint8_t *base;
    volatile uint8_t *op_base;
    volatile uint8_t *rt_base;   /* Runtime registers */
    volatile uint8_t *db_base;   /* Doorbell registers */
    uint8_t           cap_length;
    int               num_ports;
    int               max_slots;
    int               context_size;  /* 32 or 64 bytes */
    
    /* DCBA array */
    uint64_t         *dcbaap;
    uint32_t          dcbaap_phys;
    
    /* Command ring */
    xhci_trb_t       *cmd_ring;
    uint32_t          cmd_ring_phys;
    int               cmd_ring_enqueue;
    int               cmd_ring_cycle;
    
    /* Event ring */
    xhci_trb_t       *evt_ring;
    uint32_t          evt_ring_phys;
    int               evt_ring_dequeue;
    int               evt_ring_cycle;
    
    /* Event Ring Segment Table */
    uint64_t         *erst;
    uint32_t          erst_phys;
    
    /* Transfer rings (one per device/endpoint) */
    xhci_trb_t       *xfer_rings[XHCI_MAX_SLOTS + 1];  /* Per slot */
    uint32_t          xfer_ring_phys[XHCI_MAX_SLOTS + 1];
    int               xfer_ring_enqueue[XHCI_MAX_SLOTS + 1];
    int               xfer_ring_cycle[XHCI_MAX_SLOTS + 1];
    
    /* Interrupt endpoint transfer rings */
    xhci_trb_t       *int_rings[XHCI_MAX_SLOTS + 1];
    uint32_t          int_ring_phys[XHCI_MAX_SLOTS + 1];
    int               int_ring_enqueue[XHCI_MAX_SLOTS + 1];
    int               int_ring_cycle[XHCI_MAX_SLOTS + 1];
    
    /* Device contexts */
    void             *device_ctx[XHCI_MAX_SLOTS + 1];
    uint32_t          device_ctx_phys[XHCI_MAX_SLOTS + 1];
    
    /* Input contexts */
    void             *input_ctx[XHCI_MAX_SLOTS + 1];
    uint32_t          input_ctx_phys[XHCI_MAX_SLOTS + 1];
    
    bool              initialized;
    int               slot_for_mouse[MAX_USB_MICE]; /* Which slot has a mouse */
} xhci_controller_t;

#define MAX_XHCI_CONTROLLERS 4
static xhci_controller_t xhci_controllers[MAX_XHCI_CONTROLLERS];
static int xhci_controller_count = 0;

/* Write a TRB to a ring and advance enqueue pointer */
static void xhci_ring_write(xhci_trb_t *ring, int *enqueue, int *cycle, int ring_size,
                              uint32_t param_lo, uint32_t param_hi, uint32_t status, uint32_t control) {
    int idx = *enqueue;
    ring[idx].param_lo = param_lo;
    ring[idx].param_hi = param_hi;
    ring[idx].status = status;
    ring[idx].control = (control & ~0x01) | (*cycle & 0x01);
    memory_barrier();
    
    idx++;
    if (idx >= ring_size - 1) {
        /* Write link TRB */
        ring[idx].param_lo = virt_to_phys(&ring[0]) & 0xFFFFFFFF;
        ring[idx].param_hi = 0;
        ring[idx].status = 0;
        ring[idx].control = (XHCI_TRB_LINK << 10) | (1 << 1) | (*cycle & 0x01); /* Toggle cycle */
        memory_barrier();
        *cycle ^= 1;
        idx = 0;
    }
    *enqueue = idx;
}

/* Ring doorbell */
static void xhci_ring_doorbell(xhci_controller_t *hc, int slot, uint32_t value) {
    mmio_write32(hc->db_base + slot * 4, value);
}

/* Wait for command completion */
static int xhci_wait_event(xhci_controller_t *hc, uint32_t *out_param, uint32_t *out_status, int timeout_ms) {
    while (timeout_ms-- > 0) {
        memory_barrier();
        xhci_trb_t *evt = &hc->evt_ring[hc->evt_ring_dequeue];
        
        if ((evt->control & 0x01) == (uint32_t)(hc->evt_ring_cycle & 0x01)) {
            /* Event available */
            if (out_param) *out_param = evt->param_lo;
            if (out_status) *out_status = evt->status;
            
            uint32_t trb_type = (evt->control >> 10) & 0x3F;
            int cc = (evt->status >> 24) & 0xFF; /* Completion code */
            
            /* Advance dequeue */
            hc->evt_ring_dequeue++;
            if (hc->evt_ring_dequeue >= XHCI_EVT_RING_SIZE) {
                hc->evt_ring_dequeue = 0;
                hc->evt_ring_cycle ^= 1;
            }
            
            /* Write ERDP to acknowledge */
            uint32_t erdp = virt_to_phys(&hc->evt_ring[hc->evt_ring_dequeue]);
            mmio_write64(hc->rt_base + 0x38, (uint64_t)erdp | (1 << 3)); /* EHB bit */
            
            (void)trb_type;
            return cc;
        }
        mdelay(1);
    }
    return -1; /* Timeout */
}

static bool xhci_init_controller(int pci_idx) {
    if (xhci_controller_count >= MAX_XHCI_CONTROLLERS) return false;
    
    pci_usb_device_t *pci = &usb_controllers[pci_idx];
    xhci_controller_t *hc = &xhci_controllers[xhci_controller_count];
    
    /* xHCI uses MMIO BAR0 (may be 64-bit) */
    uint32_t bar0 = pci->bar[0] & 0xFFFFF000;
    if (bar0 == 0) return false;
    
    hc->base = (volatile uint8_t *)(uintptr_t)bar0;
    hc->cap_length = mmio_read8(hc->base + XHCI_CAP_CAPLENGTH);
    hc->op_base = hc->base + hc->cap_length;
    
    uint32_t rtsoff = mmio_read32(hc->base + XHCI_CAP_RTSOFF) & ~0x1F;
    hc->rt_base = hc->base + rtsoff;
    
    uint32_t dboff = mmio_read32(hc->base + XHCI_CAP_DBOFF) & ~0x03;
    hc->db_base = hc->base + dboff;
    
    /* Read capabilities */
    uint32_t hcsparams1 = mmio_read32(hc->base + XHCI_CAP_HCSPARAMS1);
    hc->max_slots = hcsparams1 & 0xFF;
    hc->num_ports = (hcsparams1 >> 24) & 0xFF;
    
    uint32_t hccparams1 = mmio_read32(hc->base + XHCI_CAP_HCCPARAMS1);
    hc->context_size = (hccparams1 & (1 << 2)) ? 64 : 32;
    
    if (hc->max_slots > XHCI_MAX_SLOTS) hc->max_slots = XHCI_MAX_SLOTS;
    
    /* Take ownership from BIOS */
    uint16_t ext_cap_off = (hccparams1 >> 16) & 0xFFFF;
    if (ext_cap_off) {
        ext_cap_off <<= 2;
        volatile uint8_t *ext = hc->base + ext_cap_off;
        while (1) {
            uint32_t cap = mmio_read32(ext);
            uint8_t cap_id = cap & 0xFF;
            if (cap_id == 1) { /* USB Legacy Support */
                if (cap & (1 << 16)) { /* BIOS owns */
                    mmio_write32(ext, cap | (1 << 24)); /* Request OS ownership */
                    int timeout = 100;
                    while (timeout-- > 0) {
                        cap = mmio_read32(ext);
                        if (!(cap & (1 << 16))) break;
                        mdelay(10);
                    }
                }
                /* Disable SMI */
                mmio_write32(ext + 4, 0);
                break;
            }
            uint8_t next_off = (cap >> 8) & 0xFF;
            if (next_off == 0) break;
            ext += next_off * 4;
        }
    }
    
    /* Stop controller */
    uint32_t cmd = mmio_read32(hc->op_base + XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RS;
    mmio_write32(hc->op_base + XHCI_OP_USBCMD, cmd);
    
    int timeout = 100;
    while (timeout-- > 0) {
        if (mmio_read32(hc->op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH) break;
        mdelay(1);
    }
    
    /* Reset */
    mmio_write32(hc->op_base + XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    mdelay(50);
    timeout = 100;
    while (timeout-- > 0) {
        uint32_t sts = mmio_read32(hc->op_base + XHCI_OP_USBSTS);
        uint32_t cmd2 = mmio_read32(hc->op_base + XHCI_OP_USBCMD);
        if (!(cmd2 & XHCI_CMD_HCRST) && !(sts & XHCI_STS_CNR)) break;
        mdelay(1);
    }
    
    /* Configure number of device slots */
    mmio_write32(hc->op_base + XHCI_OP_CONFIG, hc->max_slots);
    
    /* Allocate DCBA array (Device Context Base Address Array) */
    uint32_t dcbaap_size = (hc->max_slots + 1) * 8; /* Array of 64-bit pointers */
    hc->dcbaap = (uint64_t *)dma_alloc(dcbaap_size, 64);
    if (!hc->dcbaap) return false;
    hc->dcbaap_phys = virt_to_phys(hc->dcbaap);
    
    for (int i = 0; i <= hc->max_slots; i++) {
        hc->dcbaap[i] = 0;
    }
    
    /* Set DCBAAP */
    mmio_write64(hc->op_base + XHCI_OP_DCBAAP, (uint64_t)hc->dcbaap_phys);
    
    /* Allocate command ring */
    hc->cmd_ring = (xhci_trb_t *)dma_alloc(sizeof(xhci_trb_t) * XHCI_CMD_RING_SIZE, 64);
    if (!hc->cmd_ring) return false;
    hc->cmd_ring_phys = virt_to_phys(hc->cmd_ring);
    hc->cmd_ring_enqueue = 0;
    hc->cmd_ring_cycle = 1;
    
    /* Set CRCR */
    mmio_write64(hc->op_base + XHCI_OP_CRCR, (uint64_t)hc->cmd_ring_phys | 1);
    
    /* Allocate event ring */
    hc->evt_ring = (xhci_trb_t *)dma_alloc(sizeof(xhci_trb_t) * XHCI_EVT_RING_SIZE, 64);
    if (!hc->evt_ring) return false;
    hc->evt_ring_phys = virt_to_phys(hc->evt_ring);
    hc->evt_ring_dequeue = 0;
    hc->evt_ring_cycle = 1;
    
    /* All event TRBs start with cycle bit = 0 (not owned by us yet) */
    for (int i = 0; i < XHCI_EVT_RING_SIZE; i++) {
        hc->evt_ring[i].control = 0;
    }
    
    /* Allocate ERST (Event Ring Segment Table) */
    hc->erst = (uint64_t *)dma_alloc(32, 64); /* One entry = 16 bytes */
    if (!hc->erst) return false;
    hc->erst_phys = virt_to_phys(hc->erst);
    
    /* ERST entry: base address (64-bit) + segment size (32-bit) + reserved (32-bit) */
    hc->erst[0] = (uint64_t)hc->evt_ring_phys;
    hc->erst[1] = XHCI_EVT_RING_SIZE; /* Low 32 bits = size, high 32 = reserved */
    
    /* Set interrupter 0 registers */
    volatile uint8_t *ir0 = hc->rt_base + 0x20; /* Interrupter 0 offset */
    mmio_write32(ir0 + 0x08, XHCI_EVT_RING_SIZE);  /* ERSTSZ */
    mmio_write64(ir0 + 0x10, (uint64_t)hc->evt_ring_phys); /* ERDP */
    mmio_write64(ir0 + 0x18, (uint64_t)hc->erst_phys);     /* ERSTBA */
    
    /* Enable interrupter (but we poll, so no actual IRQ needed) */
    mmio_write32(ir0 + 0x00, mmio_read32(ir0 + 0x00) | (1 << 1)); /* IE bit */
    
    /* Start controller */
    cmd = mmio_read32(hc->op_base + XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RS | XHCI_CMD_INTE;
    mmio_write32(hc->op_base + XHCI_OP_USBCMD, cmd);
    mdelay(10);
    
    /* Initialize transfer ring tracking */
    for (int i = 0; i <= XHCI_MAX_SLOTS; i++) {
        hc->xfer_rings[i] = NULL;
        hc->int_rings[i] = NULL;
        hc->device_ctx[i] = NULL;
        hc->input_ctx[i] = NULL;
    }
    
    hc->initialized = true;
    xhci_controller_count++;
    
    return true;
}

/* Enable a slot on xHCI */
static int xhci_enable_slot(xhci_controller_t *hc) {
    xhci_ring_write(hc->cmd_ring, &hc->cmd_ring_enqueue, &hc->cmd_ring_cycle,
                     XHCI_CMD_RING_SIZE, 0, 0, 0, (XHCI_TRB_ENABLE_SLOT << 10));
    xhci_ring_doorbell(hc, 0, 0); /* Ring command doorbell */
    
    uint32_t param, status;
    int cc = xhci_wait_event(hc, &param, &status, 5000);
    if (cc != 1) return -1; /* CC=1 = Success */
    
    int slot = (status >> 24) & 0xFF;
    
    /* Allocate device context */
    int ctx_size = hc->context_size * 32; /* Slot + 31 endpoint contexts */
    hc->device_ctx[slot] = dma_alloc(ctx_size, 64);
    if (!hc->device_ctx[slot]) return -1;
    hc->device_ctx_phys[slot] = virt_to_phys(hc->device_ctx[slot]);
    
    /* Set DCBA entry */
    hc->dcbaap[slot] = (uint64_t)hc->device_ctx_phys[slot];
    memory_barrier();
    
    /* Allocate input context */
    int input_size = hc->context_size * 33; /* Control + Slot + 31 EPs */
    hc->input_ctx[slot] = dma_alloc(input_size, 64);
    if (!hc->input_ctx[slot]) return -1;
    hc->input_ctx_phys[slot] = virt_to_phys(hc->input_ctx[slot]);
    
    /* Allocate transfer ring for EP0 */
    hc->xfer_rings[slot] = (xhci_trb_t *)dma_alloc(sizeof(xhci_trb_t) * XHCI_XFER_RING_SIZE, 64);
    if (!hc->xfer_rings[slot]) return -1;
    hc->xfer_ring_phys[slot] = virt_to_phys(hc->xfer_rings[slot]);
    hc->xfer_ring_enqueue[slot] = 0;
    hc->xfer_ring_cycle[slot] = 1;
    
    return slot;
}

/* Address a device on xHCI */
static bool xhci_address_device(xhci_controller_t *hc, int slot, int port, int speed) {
    uint8_t *input = (uint8_t *)hc->input_ctx[slot];
    int cs = hc->context_size;
    
    /* Zero the input context */
    for (int i = 0; i < cs * 33; i++) input[i] = 0;
    
    /* Input Control Context */
    uint32_t *icc = (uint32_t *)input;
    icc[1] = 0x03; /* Add flags: slot context (bit 0) + EP0 (bit 1) */
    
    /* Slot Context (at offset cs) */
    uint32_t *slot_ctx = (uint32_t *)(input + cs);
    /* Context Entries = 1, Speed, Route String = 0 */
    uint32_t speed_val;
    switch (speed) {
        case 1: speed_val = 2; break; /* Low Speed */
        case 2: speed_val = 1; break; /* Full Speed */
        case 3: speed_val = 3; break; /* High Speed */
        case 4: speed_val = 4; break; /* Super Speed */
        default: speed_val = 3; break;
    }
    slot_ctx[0] = (1 << 27) | (speed_val << 20); /* 1 context entry (EP0) + speed */
    slot_ctx[1] = (port + 1) << 16; /* Root Hub Port Number (1-based) */
    
    /* EP0 Context (at offset cs * 2) */
    uint32_t *ep0_ctx = (uint32_t *)(input + cs * 2);
    uint32_t max_packet;
    switch (speed_val) {
        case 2: max_packet = 8; break;   /* Low speed: 8 bytes */
        case 1: max_packet = 64; break;  /* Full speed: 8-64 */
        case 3: max_packet = 64; break;  /* High speed: 64 */
        case 4: max_packet = 512; break; /* Super speed: 512 */
        default: max_packet = 64; break;
    }
    ep0_ctx[1] = (max_packet << 16) | (0 << 3) | (4 << 1) | (3 << 0); 
    /* MaxPacketSize, EP Type = Control (4), CErr=3 */
    /* Actually: field2 bits: MaxPacketSize[31:16], EPType[5:3]=Control bidirectional=4, CErr[2:1]=3 */
    ep0_ctx[1] = (max_packet << 16) | (4 << 3) | (3 << 1);
    ep0_ctx[2] = hc->xfer_ring_phys[slot] | hc->xfer_ring_cycle[slot]; /* TR Dequeue + DCS */
    ep0_ctx[3] = 0;
    ep0_ctx[4] = 8; /* Average TRB Length */
    
    /* Issue Address Device command */
    xhci_ring_write(hc->cmd_ring, &hc->cmd_ring_enqueue, &hc->cmd_ring_cycle,
                     XHCI_CMD_RING_SIZE,
                     hc->input_ctx_phys[slot], 0, 0,
                     (XHCI_TRB_ADDRESS_DEV << 10) | (slot << 24));
    xhci_ring_doorbell(hc, 0, 0);
    
    uint32_t param, status;
    int cc = xhci_wait_event(hc, &param, &status, 5000);
    return (cc == 1);
}

/* xHCI control transfer */
static int xhci_control_transfer(xhci_controller_t *hc, int slot,
                                   usb_setup_packet_t *setup, void *data, uint16_t data_len) {
    /* SETUP stage TRB */
    uint32_t setup_lo = *(uint32_t *)setup;
    uint32_t setup_hi = *((uint32_t *)setup + 1);
    uint32_t trt = 0; /* Transfer Type */
    if (data_len > 0) {
        trt = (setup->bmRequestType & USB_DIR_IN) ? 3 : 2; /* IN=3, OUT=2 */
    }
    
    xhci_ring_write(hc->xfer_rings[slot], &hc->xfer_ring_enqueue[slot],
                     &hc->xfer_ring_cycle[slot], XHCI_XFER_RING_SIZE,
                     setup_lo, setup_hi, 8, /* TRB Transfer Length = 8 for setup */
                     (XHCI_TRB_SETUP << 10) | (1 << 6) | (trt << 16)); /* IDT=1 */
    
    /* DATA stage TRB (if needed) */
    void *data_buf = NULL;
    if (data_len > 0 && data) {
        data_buf = dma_alloc(data_len, 16);
        if (!data_buf) return -1;
        
        if (!(setup->bmRequestType & USB_DIR_IN)) {
            uint8_t *s = (uint8_t *)data;
            uint8_t *d2 = (uint8_t *)data_buf;
            for (int i = 0; i < data_len; i++) d2[i] = s[i];
        }
        
        uint32_t dir = (setup->bmRequestType & USB_DIR_IN) ? (1 << 16) : 0;
        xhci_ring_write(hc->xfer_rings[slot], &hc->xfer_ring_enqueue[slot],
                         &hc->xfer_ring_cycle[slot], XHCI_XFER_RING_SIZE,
                         virt_to_phys(data_buf), 0, data_len,
                         (XHCI_TRB_DATA << 10) | dir);
    }
    
    /* STATUS stage TRB */
    uint32_t status_dir = (data_len > 0 && (setup->bmRequestType & USB_DIR_IN)) ? 0 : (1 << 16);
    xhci_ring_write(hc->xfer_rings[slot], &hc->xfer_ring_enqueue[slot],
                     &hc->xfer_ring_cycle[slot], XHCI_XFER_RING_SIZE,
                     0, 0, 0,
                     (XHCI_TRB_STATUS << 10) | (1 << 5) | status_dir); /* IOC=1 */
    
    /* Ring EP0 doorbell (target = 1 for EP0) */
    xhci_ring_doorbell(hc, slot, 1);
    
    /* Wait for completion */
    uint32_t param, status;
    int cc = xhci_wait_event(hc, &param, &status, 5000);
    
    if (cc != 1 && cc != 13) { /* 1=Success, 13=Short Packet */
        return -3;
    }
    
    if (data_buf && (setup->bmRequestType & USB_DIR_IN)) {
        uint8_t *s = (uint8_t *)data_buf;
        uint8_t *d2 = (uint8_t *)data;
        for (int i = 0; i < data_len; i++) d2[i] = s[i];
    }
    
    return data_len;
}

/* Configure an interrupt endpoint on xHCI */
static void xhci_setup_interrupt_endpoint(xhci_controller_t *hc, int slot, 
                                            usb_mouse_device_t *dev) {
    /* Allocate interrupt transfer ring */
    hc->int_rings[slot] = (xhci_trb_t *)dma_alloc(sizeof(xhci_trb_t) * XHCI_XFER_RING_SIZE, 64);
    if (!hc->int_rings[slot]) return;
    hc->int_ring_phys[slot] = virt_to_phys(hc->int_rings[slot]);
    hc->int_ring_enqueue[slot] = 0;
    hc->int_ring_cycle[slot] = 1;
    
    /* Allocate transfer buffer */
    dev->transfer_buffer = dma_alloc(dev->max_packet_size, 16);
    if (!dev->transfer_buffer) return;
    dev->transfer_phys = virt_to_phys(dev->transfer_buffer);
    
    /* Configure endpoint via Configure Endpoint command */
    uint8_t *input = (uint8_t *)hc->input_ctx[slot];
    int cs = hc->context_size;
    
    /* Zero input context */
    for (int i = 0; i < cs * 33; i++) input[i] = 0;
    
    uint32_t *icc = (uint32_t *)input;
    /* DCI (Device Context Index) for interrupt IN endpoint:
     * DCI = (endpoint_number * 2) + direction (1 for IN) */
    int dci = (dev->endpoint * 2) + 1;
    icc[1] = (1 << 0) | (1 << dci); /* Add slot context + endpoint */
    
    /* Update slot context entries count */
    uint32_t *slot_ctx = (uint32_t *)(input + cs);
    uint32_t *dev_slot = (uint32_t *)hc->device_ctx[slot];
    slot_ctx[0] = (dev_slot[0] & 0x0FFFFFFF) | (dci << 27); /* Update context entries */
    slot_ctx[1] = dev_slot[1];
    slot_ctx[2] = dev_slot[2];
    slot_ctx[3] = dev_slot[3];
    
    /* Setup interrupt endpoint context */
    uint32_t *ep_ctx = (uint32_t *)(input + cs * (dci + 1));
    /* EP Type: Interrupt IN = 7 */
    uint32_t interval_exp = 0;
    int intv = dev->interval;
    while (intv > 1) { intv >>= 1; interval_exp++; }
    
    ep_ctx[0] = (interval_exp << 16); /* Interval */
    ep_ctx[1] = ((uint32_t)dev->max_packet_size << 16) | (7 << 3) | (3 << 1); /* MaxPacket, EPType=7, CErr=3 */
    ep_ctx[2] = hc->int_ring_phys[slot] | hc->int_ring_cycle[slot];
    ep_ctx[3] = 0;
    ep_ctx[4] = dev->max_packet_size; /* Average TRB Length */
    
    /* Issue Configure Endpoint command */
    xhci_ring_write(hc->cmd_ring, &hc->cmd_ring_enqueue, &hc->cmd_ring_cycle,
                     XHCI_CMD_RING_SIZE,
                     hc->input_ctx_phys[slot], 0, 0,
                     (XHCI_TRB_CONFIG_EP << 10) | (slot << 24));
    xhci_ring_doorbell(hc, 0, 0);
    
    uint32_t param, xstatus;
    int cc = xhci_wait_event(hc, &param, &xstatus, 5000);
    if (cc != 1) return;
    
    /* Submit initial interrupt IN transfer */
    xhci_ring_write(hc->int_rings[slot], &hc->int_ring_enqueue[slot],
                     &hc->int_ring_cycle[slot], XHCI_XFER_RING_SIZE,
                     dev->transfer_phys, 0, dev->max_packet_size,
                     (XHCI_TRB_NORMAL << 10) | (1 << 5)); /* IOC=1 */
    
    /* Ring interrupt endpoint doorbell */
    xhci_ring_doorbell(hc, slot, dci);
    
    dev->td_pool = hc->int_rings[slot];
    dev->td_pool_phys = hc->int_ring_phys[slot];
}

/* Poll xHCI for mouse events */
static void xhci_poll_mouse(xhci_controller_t *hc, usb_mouse_device_t *dev, int slot) {
    if (!dev->transfer_buffer || !hc->int_rings[slot]) return;
    
    /* Check event ring */
    memory_barrier();
    xhci_trb_t *evt = &hc->evt_ring[hc->evt_ring_dequeue];
    
    if ((evt->control & 0x01) != (uint32_t)(hc->evt_ring_cycle & 0x01)) return;
    
    uint32_t trb_type = (evt->control >> 10) & 0x3F;
    int cc = (evt->status >> 24) & 0xFF;
    
    /* Advance dequeue */
    hc->evt_ring_dequeue++;
    if (hc->evt_ring_dequeue >= XHCI_EVT_RING_SIZE) {
        hc->evt_ring_dequeue = 0;
        hc->evt_ring_cycle ^= 1;
    }
    
    /* Acknowledge */
    uint32_t erdp = virt_to_phys(&hc->evt_ring[hc->evt_ring_dequeue]);
    mmio_write64(hc->rt_base + 0x38, (uint64_t)erdp | (1 << 3));
    
    if (trb_type == XHCI_TRB_TRANSFER_EVT && (cc == 1 || cc == 13)) {
        uint8_t *report = (uint8_t *)dev->transfer_buffer;
        int residual = evt->status & 0xFFFFFF;
        int actual = dev->max_packet_size - residual;
        
        if (actual >= 3) {
            uint8_t btn = 0;
            if (report[0] & 0x01) btn |= MOUSE_BUTTON_LEFT;
            if (report[0] & 0x02) btn |= MOUSE_BUTTON_RIGHT;
            if (report[0] & 0x04) btn |= MOUSE_BUTTON_MIDDLE;
            if (report[0] & 0x08) btn |= MOUSE_BUTTON_4;
            if (report[0] & 0x10) btn |= MOUSE_BUTTON_5;
            
            int dx = mouse_accelerate((int8_t)report[1]);
            int dy = mouse_accelerate((int8_t)report[2]);
            int dz = (actual >= 4) ? (int8_t)report[3] : 0;
            
            mouse_apply_movement(dx, dy);
            
            mouse_clamp_position();
            mouse.z_accum += dz;
            mouse_update_buttons(btn);
            
            mouse_event_t evt2 = {
                .x = mouse.x, .y = mouse.y,
                .dx = dx, .dy = dy, .dz = dz,
                .buttons = mouse.buttons,
                .absolute = false,
                .timestamp = rdtsc()
            };
            mouse_queue_event(&evt2);
            mouse.total_packets++;
        }
    }
    
    /* Resubmit transfer */
    xhci_ring_write(hc->int_rings[slot], &hc->int_ring_enqueue[slot],
                     &hc->int_ring_cycle[slot], XHCI_XFER_RING_SIZE,
                     dev->transfer_phys, 0, dev->max_packet_size,
                     (XHCI_TRB_NORMAL << 10) | (1 << 5));
    
    int dci = (dev->endpoint * 2) + 1;
    xhci_ring_doorbell(hc, slot, dci);
}

/* Reset an xHCI port */
static bool xhci_port_reset(xhci_controller_t *hc, int port) {
    volatile uint8_t *portsc = hc->op_base + XHCI_OP_PORTS_BASE + port * 0x10 + XHCI_PORT_PORTSC;
    
    uint32_t status = mmio_read32(portsc);
    if (!(status & XHCI_PORT_CCS)) return false;
    
    /* Issue port reset */
    mmio_write32(portsc, (status & 0x0E01C3E0) | XHCI_PORT_PR);
    
    /* Wait for reset complete (PRC bit) */
    int timeout = 200;
    while (timeout-- > 0) {
        mdelay(5);
        status = mmio_read32(portsc);
        if (status & XHCI_PORT_PRC) {
            /* Clear PRC */
            mmio_write32(portsc, (status & 0x0E01C3E0) | XHCI_PORT_PRC);
            mdelay(10);
            
            status = mmio_read32(portsc);
            if (status & XHCI_PORT_PED) return true;
        }
    }
    
    return false;
}

static int xhci_port_speed(xhci_controller_t *hc, int port) {
    uint32_t portsc = mmio_read32(hc->op_base + XHCI_OP_PORTS_BASE + port * 0x10);
    return (portsc >> XHCI_PORT_SPEED_SHIFT) & 0x0F;
}

/* Section 17: USB Device Enumeration (Generic) */

/* Generic control transfer dispatcher */
static int usb_control_transfer(usb_mouse_device_t *dev, usb_setup_packet_t *setup,
                                 void *data, uint16_t data_len) {
    switch (dev->controller_type) {
        case 0: { /* UHCI */
            uhci_controller_t *hc = &uhci_controllers[dev->controller_idx];
            return uhci_control_transfer(hc, dev->address, true /* TODO: track speed */, 
                                          setup, data, data_len);
        }
        case 1: { /* OHCI */
            ohci_controller_t *hc = &ohci_controllers[dev->controller_idx];
            return ohci_control_transfer(hc, dev->address, true, setup, data, data_len);
        }
        case 2: { /* EHCI */
            ehci_controller_t *hc = &ehci_controllers[dev->controller_idx];
            return ehci_control_transfer(hc, dev->address, setup, data, data_len);
        }
        case 3: { /* xHCI - uses slot ID, not address */
            /* For xHCI we stored the slot in a different way */
            return -1; /* Handled separately */
        }
    }
    return -1;
}

/* Check if a USB device is a HID Boot Protocol mouse */
static bool usb_is_boot_mouse(uint8_t *config_desc, int config_len,
                                uint8_t *out_endpoint, uint8_t *out_max_packet,
                                uint8_t *out_interval, uint8_t *out_interface) {
    int offset = 0;
    
    while (offset < config_len - 2) {
        uint8_t desc_len = config_desc[offset];
        uint8_t desc_type = config_desc[offset + 1];
        
        if (desc_len == 0) break;
        if (offset + desc_len > config_len) break;
        
        if (desc_type == USB_DESC_INTERFACE && desc_len >= 9) {
            usb_interface_descriptor_t *iface = (usb_interface_descriptor_t *)&config_desc[offset];
            
            if (iface->bInterfaceClass == USB_CLASS_HID &&
                iface->bInterfaceSubClass == USB_SUBCLASS_BOOT &&
                iface->bInterfaceProtocol == USB_PROTOCOL_MOUSE) {
                
                *out_interface = iface->bInterfaceNumber;
                
                /* Find the interrupt IN endpoint */
                int ep_offset = offset + desc_len;
                while (ep_offset < config_len - 2) {
                    uint8_t ep_len = config_desc[ep_offset];
                    uint8_t ep_type = config_desc[ep_offset + 1];
                    
                    if (ep_len == 0) break;
                    if (ep_type == USB_DESC_INTERFACE) break; /* Next interface */
                    
                    if (ep_type == USB_DESC_ENDPOINT && ep_len >= 7) {
                        usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t *)&config_desc[ep_offset];
                        
                        if ((ep->bEndpointAddress & 0x80) && /* IN */
                            (ep->bmAttributes & 0x03) == 0x03) { /* Interrupt */
                            *out_endpoint = ep->bEndpointAddress & 0x0F;
                            *out_max_packet = ep->wMaxPacketSize & 0xFF;
                            *out_interval = ep->bInterval;
                            return true;
                        }
                    }
                    ep_offset += ep_len;
                }
            }
        }
        offset += desc_len;
    }
    return false;
}

/* Enumerate a USB device and check if it's a mouse */
static bool usb_enumerate_device(int ctrl_type, int ctrl_idx, int port) {
    if (usb_mouse_count >= MAX_USB_MICE) return false;
    
    usb_mouse_device_t *mdev = &usb_mice[usb_mouse_count];
    mdev->controller_type = ctrl_type;
    mdev->controller_idx = ctrl_idx;
    mdev->address = 0;
    mdev->toggle = 0;
    mdev->active = false;
    
    usb_setup_packet_t setup;
    
    /* For xHCI, the process is different - use slot-based addressing */
    if (ctrl_type == 3) {
        xhci_controller_t *hc = &xhci_controllers[ctrl_idx];
        
        int slot = xhci_enable_slot(hc);
        if (slot < 0) return false;
        
        int speed = xhci_port_speed(hc, port);
        if (!xhci_address_device(hc, slot, port, speed)) return false;
        
        /* Get device descriptor */
        usb_device_descriptor_t dev_desc;
        setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        setup.bRequest = USB_REQ_GET_DESCRIPTOR;
        setup.wValue = (USB_DESC_DEVICE << 8) | 0;
        setup.wIndex = 0;
        setup.wLength = sizeof(dev_desc);
        
        int ret = xhci_control_transfer(hc, slot, &setup, &dev_desc, sizeof(dev_desc));
        if (ret < 0) return false;
        
        /* Get configuration descriptor */
        uint8_t config_buf[256];
        setup.wValue = (USB_DESC_CONFIGURATION << 8) | 0;
        setup.wLength = sizeof(config_buf);
        
        ret = xhci_control_transfer(hc, slot, &setup, config_buf, sizeof(config_buf));
        if (ret < 0) return false;
        
        uint8_t ep_addr, ep_max_packet, ep_interval, iface_num;
        if (!usb_is_boot_mouse(config_buf, ret, &ep_addr, &ep_max_packet, &ep_interval, &iface_num)) {
            return false;
        }
        
        /* Set Configuration */
        usb_config_descriptor_t *cfg = (usb_config_descriptor_t *)config_buf;
        setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
        setup.bRequest = USB_REQ_SET_CONFIG;
        setup.wValue = cfg->bConfigurationValue;
        setup.wIndex = 0;
        setup.wLength = 0;
        xhci_control_transfer(hc, slot, &setup, NULL, 0);
        
        /* Set Boot Protocol */
        setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
        setup.bRequest = USB_REQ_SET_PROTOCOL;
        setup.wValue = 0; /* Boot protocol */
        setup.wIndex = iface_num;
        setup.wLength = 0;
        xhci_control_transfer(hc, slot, &setup, NULL, 0);
        
        /* Set Idle (rate=0 = only report on change) */
        setup.bRequest = USB_REQ_SET_IDLE;
        setup.wValue = 0;
        setup.wIndex = iface_num;
        setup.wLength = 0;
        xhci_control_transfer(hc, slot, &setup, NULL, 0);
        
        mdev->endpoint = ep_addr;
        mdev->max_packet_size = ep_max_packet;
        mdev->interval = ep_interval;
        mdev->interface_num = iface_num;
        mdev->boot_protocol = true;
        mdev->active = true;
        
        /* Setup interrupt endpoint for periodic polling */
        xhci_setup_interrupt_endpoint(hc, slot, mdev);
        
        hc->slot_for_mouse[usb_mouse_count] = slot;
        usb_mouse_count++;
        mouse.usb_active = true;
        return true;
    }
    
    /* For UHCI/OHCI/EHCI: classic address-based enumeration */
    
    /* Step 1: Get device descriptor with address 0 (first 8 bytes) */
    uint8_t desc_buf[18];
    setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 8;
    
    int ret = usb_control_transfer(mdev, &setup, desc_buf, 8);
    if (ret < 0) return false;
    
    uint8_t max_packet0 = desc_buf[7];
    if (max_packet0 == 0) max_packet0 = 8;
    
    /* Step 2: Set Address */
    uint8_t new_addr = next_usb_address++;
    if (next_usb_address > 127) next_usb_address = 1;
    
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = new_addr;
    setup.wIndex = 0;
    setup.wLength = 0;
    
    ret = usb_control_transfer(mdev, &setup, NULL, 0);
    if (ret < 0) return false;
    
    mdelay(10); /* Recovery time after SET_ADDRESS */
    mdev->address = new_addr;
    
    /* Step 3: Get full device descriptor */
    setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 18;
    
    ret = usb_control_transfer(mdev, &setup, desc_buf, 18);
    if (ret < 0) return false;
    
    /* Step 4: Get configuration descriptor */
    uint8_t config_buf[256];
    setup.wValue = (USB_DESC_CONFIGURATION << 8) | 0;
    setup.wLength = 4; /* Just header first */
    
    ret = usb_control_transfer(mdev, &setup, config_buf, 4);
    if (ret < 4) return false;
    
    uint16_t total_len = config_buf[2] | (config_buf[3] << 8);
    if (total_len > sizeof(config_buf)) total_len = sizeof(config_buf);
    
    setup.wLength = total_len;
    ret = usb_control_transfer(mdev, &setup, config_buf, total_len);
    if (ret < 0) return false;
    
    /* Step 5: Check if it's a HID Boot mouse */
    uint8_t ep_addr, ep_max_packet, ep_interval, iface_num;
    if (!usb_is_boot_mouse(config_buf, total_len, &ep_addr, &ep_max_packet, &ep_interval, &iface_num)) {
        return false; /* Not a mouse */
    }
    
    /* Step 6: Set Configuration */
    usb_config_descriptor_t *cfg = (usb_config_descriptor_t *)config_buf;
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_CONFIG;
    setup.wValue = cfg->bConfigurationValue;
    setup.wIndex = 0;
    setup.wLength = 0;
    usb_control_transfer(mdev, &setup, NULL, 0);
    mdelay(10);
    
    /* Step 7: Set Boot Protocol */
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    setup.bRequest = USB_REQ_SET_PROTOCOL;
    setup.wValue = 0; /* 0 = Boot Protocol */
    setup.wIndex = iface_num;
    setup.wLength = 0;
    usb_control_transfer(mdev, &setup, NULL, 0);
    
    /* Step 8: Set Idle */
    setup.bRequest = USB_REQ_SET_IDLE;
    setup.wValue = 0; /* Indefinite idle */
    setup.wIndex = iface_num;
    setup.wLength = 0;
    usb_control_transfer(mdev, &setup, NULL, 0);
    
    mdev->endpoint = ep_addr;
    mdev->max_packet_size = ep_max_packet;
    mdev->interval = ep_interval;
    mdev->interface_num = iface_num;
    mdev->boot_protocol = true;
    mdev->poll_interval_us = (uint32_t)ep_interval * 1000;
    mdev->active = true;
    
    /* Setup interrupt polling for this mouse */
    switch (ctrl_type) {
        case 0:
            uhci_setup_interrupt_polling(&uhci_controllers[ctrl_idx], mdev);
            break;
        case 1:
            ohci_setup_interrupt_polling(&ohci_controllers[ctrl_idx], mdev);
            break;
        case 2:
            ehci_setup_interrupt_polling(&ehci_controllers[ctrl_idx], mdev);
            break;
    }
    
    usb_mouse_count++;
    mouse.usb_active = true;
    return true;
}

/* Scan all USB controllers for mice */
static void usb_scan_for_mice(void) {
    /* Scan UHCI controllers */
    for (int c = 0; c < uhci_controller_count; c++) {
        uhci_controller_t *hc = &uhci_controllers[c];
        if (!hc->initialized) continue;
        
        for (int p = 0; p < hc->num_ports; p++) {
            if (uhci_port_reset(hc, p)) {
                mdelay(100);
                usb_enumerate_device(0, c, p);
            }
        }
    }
    
    /* Scan OHCI controllers */
    for (int c = 0; c < ohci_controller_count; c++) {
        ohci_controller_t *hc = &ohci_controllers[c];
        if (!hc->initialized) continue;
        
        for (int p = 0; p < hc->num_ports; p++) {
            if (ohci_port_reset(hc, p)) {
                mdelay(100);
                usb_enumerate_device(1, c, p);
            }
        }
    }
    
    /* Scan EHCI controllers */
    for (int c = 0; c < ehci_controller_count; c++) {
        ehci_controller_t *hc = &ehci_controllers[c];
        if (!hc->initialized) continue;
        
        for (int p = 0; p < hc->num_ports; p++) {
            if (ehci_port_reset(hc, p)) {
                mdelay(100);
                usb_enumerate_device(2, c, p);
            }
        }
    }
    
    /* Scan xHCI controllers */
    for (int c = 0; c < xhci_controller_count; c++) {
        xhci_controller_t *hc = &xhci_controllers[c];
        if (!hc->initialized) continue;
        
        for (int p = 0; p < hc->num_ports; p++) {
            uint32_t portsc = mmio_read32(hc->op_base + XHCI_OP_PORTS_BASE + p * 0x10);
            if (portsc & XHCI_PORT_CCS) {
                if (xhci_port_reset(hc, p)) {
                    mdelay(100);
                    usb_enumerate_device(3, c, p);
                }
            }
        }
    }
}

/* Section 18: Main initialization */

int mouse_init(void) {
    /* Initialize state */
    mouse.x = 0;
    mouse.y = 0;
    mouse.x_accum = 0;
    mouse.y_accum = 0;
    mouse.z_accum = 0;
    mouse.buttons = 0;
    mouse.left = false;
    mouse.right = false;
    mouse.middle = false;
    mouse.button4 = false;
    mouse.button5 = false;
    mouse.initialized = false;
    mouse.ps2_active = false;
    mouse.usb_active = false;
    mouse.vmmouse_active = false;
    mouse.vbox_active = false;
    mouse.qemu_tablet_active = false;
    mouse.absolute_mode = false;
    mouse.ps2_packet_idx = 0;
    mouse.ps2_type = PS2_MOUSE_TYPE_STANDARD;
    mouse.event_head = 0;
    mouse.event_tail = 0;
    mouse.total_packets = 0;
    mouse.dropped_packets = 0;
    mouse.sync_errors = 0;

    /* Default acceleration settings - DISABLED for smooth movement */
    mouse.accel_enabled = false;
    mouse.accel_numerator = 2;
    mouse.accel_denominator = 1;
    mouse.accel_threshold = 5;
    
    /* Sensitivity: 3/4 speed (75% of original) */
    mouse.sensitivity_num = 3;
    mouse.sensitivity_den = 4;

    /* Debounce: ~5ms at ~1GHz TSC */
    mouse.debounce_cycles = 5000000;

    /* Screen dimensions */
    int w = gpu_get_width();
    int h = gpu_get_height();
    if (w <= 0) w = 320;
    if (h <= 0) h = 200;
    mouse.limit_w = w;
    mouse.limit_h = h;
    mouse.x = w / 2;
    mouse.y = h / 2;

    /* Calibrate TSC for accurate delays */
    calibrate_tsc();

    /* Scan PCI bus for USB controllers */
    pci_scan_usb();

    /* Initialize USB controllers */
    for (int i = 0; i < usb_controller_count; i++) {
        switch (usb_controllers[i].type) {
            case 0: uhci_init_controller(i); break;
            case 1: ohci_init_controller(i); break;
            case 2: ehci_init_controller(i); break;
            case 3: xhci_init_controller(i); break;
        }
    }

    /* Try VMware VMMouse first (absolute positioning preferred) */
    if (vmmouse_enable()) {
        mouse.vmmouse_active = true;
        mouse.absolute_mode = true;
    }

    /* Try VirtualBox absolute mouse */
    if (!mouse.absolute_mode && vbox_mouse_detect()) {
        mouse.vbox_active = true;
        mouse.absolute_mode = true;
    }

    /* Scan USB controllers for mice */
    usb_scan_for_mice();

    /* Initialize PS/2 mouse as fallback or additional device */
    if (!mouse.usb_active || !mouse.absolute_mode) {
        if (ps2_mouse_init()) {
            mouse.ps2_active = true;
        }
    }

    /* At least one input method must be active */
    if (!mouse.ps2_active && !mouse.usb_active &&
        !mouse.vmmouse_active && !mouse.vbox_active) {
        return -1;
    }

    mouse.initialized = true;
    return 0;
}

/* Section 19: Mouse polling / update */

void mouse_poll(void) {
    if (!mouse.initialized) return;

    /* VMMouse absolute positioning (highest priority) */
    if (mouse.vmmouse_active) {
        vmmouse_poll();
        /* Also drain PS/2 to avoid buffer overflow */
        if (mouse.ps2_active) {
            ps2_mouse_poll();
        }
        return;
    }

    /* USB mice */
    if (mouse.usb_active) {
        for (int i = 0; i < usb_mouse_count; i++) {
            usb_mouse_device_t *dev = &usb_mice[i];
            if (!dev->active) continue;

            switch (dev->controller_type) {
                case 0:
                    uhci_poll_mouse(&uhci_controllers[dev->controller_idx], dev);
                    break;
                case 1:
                    ohci_poll_mouse(&ohci_controllers[dev->controller_idx], dev);
                    break;
                case 2:
                    ehci_poll_mouse(&ehci_controllers[dev->controller_idx], dev);
                    break;
                case 3: {
                    xhci_controller_t *hc = &xhci_controllers[dev->controller_idx];
                    int slot = -1;
                    for (int j = 0; j < MAX_USB_MICE; j++) {
                        if (hc->slot_for_mouse[j] > 0 && &usb_mice[j] == dev) {
                            slot = hc->slot_for_mouse[j];
                            break;
                        }
                    }
                    if (slot > 0) {
                        xhci_poll_mouse(hc, dev, slot);
                    }
                    break;
                }
            }
        }
    }

    /* PS/2 mouse (fallback or supplemental) */
    if (mouse.ps2_active) {
        ps2_mouse_poll();
    }
}

/* Section 20: Public API */

int mouse_get_x(void) {
    return mouse.x;
}

int mouse_get_y(void) {
    return mouse.y;
}

int mouse_get_buttons(void) {
    return (int)mouse.buttons;
}

bool mouse_left_pressed(void) {
    return mouse.left;
}

bool mouse_right_pressed(void) {
    return mouse.right;
}

bool mouse_middle_pressed(void) {
    return mouse.middle;
}

bool mouse_button4_pressed(void) {
    return mouse.button4;
}

bool mouse_button5_pressed(void) {
    return mouse.button5;
}

int mouse_get_scroll(void) {
    int z = mouse.z_accum;
    mouse.z_accum = 0;
    return z;
}

bool mouse_is_absolute(void) {
    return mouse.absolute_mode;
}

void mouse_set_acceleration(bool enabled, int numerator, int denominator, int threshold) {
    mouse.accel_enabled = enabled;
    if (numerator > 0)   mouse.accel_numerator   = numerator;
    if (denominator > 0) mouse.accel_denominator = denominator;
    if (threshold >= 0)  mouse.accel_threshold   = threshold;
}

void mouse_set_screen_size(int width, int height) {
    if (width  > 0) mouse.limit_w = width;
    if (height > 0) mouse.limit_h = height;
    mouse_clamp_position();
}

void mouse_set_position(int x, int y) {
    mouse.x = x;
    mouse.y = y;
    mouse_clamp_position();
}

bool mouse_get_event(int *out_x, int *out_y, int *out_dx, int *out_dy,
                      int *out_dz, uint8_t *out_buttons) {
    mouse_event_t evt;
    if (!mouse_dequeue_event(&evt)) return false;

    if (out_x)       *out_x       = evt.x;
    if (out_y)       *out_y       = evt.y;
    if (out_dx)      *out_dx      = evt.dx;
    if (out_dy)      *out_dy      = evt.dy;
    if (out_dz)      *out_dz      = evt.dz;
    if (out_buttons) *out_buttons = evt.buttons;

    return true;
}

void mouse_get_stats(uint64_t *total, uint64_t *dropped, uint64_t *sync_errors) {
    if (total)       *total       = mouse.total_packets;
    if (dropped)     *dropped     = mouse.dropped_packets;
    if (sync_errors) *sync_errors = mouse.sync_errors;
}

bool mouse_is_initialized(void) {
    return mouse.initialized;
}

bool mouse_has_ps2(void) {
    return mouse.ps2_active;
}

bool mouse_has_usb(void) {
    return mouse.usb_active;
}

int mouse_get_usb_count(void) {
    return usb_mouse_count;
}

void mouse_set_debounce(uint32_t microseconds) {
    /* Convert microseconds to TSC cycles */
    mouse.debounce_cycles = (uint64_t)microseconds * tsc_per_us;
}

/* Reset/re-sync PS/2 packet state machine */
void mouse_ps2_resync(void) {
    mouse.ps2_packet_idx = 0;
    ps2_flush();
}

/* Public API functions required by window_manager and desktop */
bool mouse_get_left(void) {
    return (mouse.buttons & 0x01) != 0;
}

bool mouse_get_right(void) {
    return (mouse.buttons & 0x02) != 0;
}

int mouse_get_z_delta(void) {
    int z = mouse.z_accum;
    mouse.z_accum = 0;
    return z;
}

void mouse_set_bounds(int width, int height) {
    if (width > 0) mouse.limit_w = width;
    if (height > 0) mouse.limit_h = height;
    mouse_clamp_position();
}
