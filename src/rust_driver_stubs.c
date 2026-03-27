/*
 * WiFi and GPU Drivers for AurionOS
 * GPU uses VGA Mode 13h (320x200x256) for reliable graphics
*/

#include <stdint.h>
#include <stdbool.h>
#include "../include/network.h"

// GOT stub for Rust PIC code
void *_GLOBAL_OFFSET_TABLE_[3] = {0, 0, 0};

extern void c_puts(const char *s);
extern void c_putc(char c);
extern void c_cls(void);
extern uint16_t c_getkey(void);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

#define puts c_puts
#define putc c_putc
#define cls c_cls
#define getkey c_getkey

/* Port I/O */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* PCI */
static uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

/* VirtIO constants */
#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_NET_DEV 0x1000
#define VIRTIO_PCI_STATUS 0x12
#define VIRTIO_PCI_CONFIG 0x14

/* WiFi/Network Driver */

/* VirtIO registers */
#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_SIZE 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10

/* VirtIO Net header */
#pragma pack(push, 1)
typedef struct
{
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} virtio_net_hdr_t;
#pragma pack(pop)

/* VirtQ descriptor - must be exactly 16 bytes for legacy VirtIO */
typedef struct
{
    uint64_t addr;  /* 64-bit guest physical address */
    uint32_t len;   /* Length of buffer */
    uint16_t flags; /* Flags (NEXT, WRITE, INDIRECT) */
    uint16_t next;  /* Next descriptor if flags & NEXT */
} __attribute__((packed)) virtq_desc_t;

/* VirtQ available ring */
typedef struct
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
} virtq_avail_t;

/* VirtQ used element */
typedef struct
{
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

/* VirtQ used ring */
typedef struct
{
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[256];
} virtq_used_t;

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define RX_QUEUE 0
#define TX_QUEUE 1
#define QUEUE_SIZE 4 /* Reduced from 16 to save memory */
#define PKT_BUF_SIZE 2048

static bool wifi_initialized = false;
static uint16_t wifi_io_base = 0;
static uint8_t wifi_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static network_interface_t net_iface;

/*
 * VirtIO queue memory layout (must be contiguous and page aligned):
 * For queue_size=256 (typical QEMU default):
 * - Descriptor Table: 16 bytes * 256 = 4096 bytes
 * - Available Ring: 6 + 2*256 = 518 bytes
 * - Padding to 4096 boundary
 * - Used Ring: 6 + 8*256 = 2054 bytes
 *
 * We allocate for max queue size 256 to be safe.
*/

#define MAX_QUEUE_SIZE 256

/* RX/TX packet buffers - we only use QUEUE_SIZE (16) of them */
static uint8_t rx_buffers[QUEUE_SIZE][PKT_BUF_SIZE] __attribute__((aligned(4096)));
static uint8_t tx_buffers[QUEUE_SIZE][PKT_BUF_SIZE] __attribute__((aligned(4096)));

/* RX Queue - allocate for 256 entries to match device expectations */
/* Layout: desc[256] at 0, avail at 4096, used at 8192 */
static uint8_t rx_queue_mem[16384] __attribute__((aligned(4096)));

/* TX Queue - same layout */
static uint8_t tx_queue_mem[16384] __attribute__((aligned(4096)));

/* Device-reported queue sizes - stored after init */
static uint16_t rx_queue_size = 256;
static uint16_t tx_queue_size = 256;

/*
 * For queue_size=256:
 * - Descriptors: 0 to 4095 (256 * 16 = 4096 bytes)
 * - Available ring: 4096 to ~4614 (6 + 2*256 = 518 bytes)
 * - Padding to 8192 boundary
 * - Used ring: 8192+ (6 + 8*256 = 2054 bytes)
*/

#define VRING_DESC_OFFSET 0
#define VRING_AVAIL_OFFSET 4096 /* 256 * 16 */
#define VRING_USED_OFFSET 8192  /* Aligned to 4096 after avail */

/* Accessor macros - fixed offsets for queue size 256 */
#define rx_desc ((virtq_desc_t *)(&rx_queue_mem[VRING_DESC_OFFSET]))
#define rx_avail ((virtq_avail_t *)(&rx_queue_mem[VRING_AVAIL_OFFSET]))
#define rx_used ((virtq_used_t *)(&rx_queue_mem[VRING_USED_OFFSET]))

#define tx_desc ((virtq_desc_t *)(&tx_queue_mem[VRING_DESC_OFFSET]))
#define tx_avail ((virtq_avail_t *)(&tx_queue_mem[VRING_AVAIL_OFFSET]))
#define tx_used ((virtq_used_t *)(&tx_queue_mem[VRING_USED_OFFSET]))

static uint16_t rx_last_used = 0;
static uint16_t tx_last_used = 0;
static uint16_t tx_free_idx = 0;
static uint16_t tx_avail_idx = 0;

static uint16_t find_virtio_net(void)
{
    for (int bus = 0; bus < 256; bus++)
    {
        for (int dev = 0; dev < 32; dev++)
        {
            uint32_t id = pci_read(bus, dev, 0, 0);
            if ((id & 0xFFFF) == VIRTIO_VENDOR)
            {
                uint16_t did = (id >> 16) & 0xFFFF;
                if (did == VIRTIO_NET_DEV || did == 0x1041)
                {
                    /* Enable bus mastering */
                    uint32_t cmd = pci_read(bus, dev, 0, 0x04);
                    cmd |= 0x07; /* I/O + Memory + Bus Master */
                    outl(0xCF8, 0x80000000 | (bus << 16) | (dev << 11) | 0x04);
                    outl(0xCFC, cmd);

                    uint32_t bar0 = pci_read(bus, dev, 0, 0x10);
                    if (bar0 & 1)
                        return (bar0 & 0xFFFC);
                }
            }
        }
    }
    return 0;
}

static void setup_virtqueue(uint16_t queue_idx, uint8_t *queue_mem, uint16_t *out_qsz)
{
    /* Select queue */
    outw(wifi_io_base + VIRTIO_PCI_QUEUE_SEL, queue_idx);

    /* Get queue size from device */
    uint16_t qsz = inw(wifi_io_base + VIRTIO_PCI_QUEUE_SIZE);
    *out_qsz = qsz;

    puts("[NET] Queue ");
    putc('0' + queue_idx);
    puts(" size=");
    putc('0' + (qsz / 100) % 10);
    putc('0' + (qsz / 10) % 10);
    putc('0' + qsz % 10);

    /* Calculate physical address of queue (must be page aligned) */
    uint32_t queue_addr = (uint32_t)queue_mem;
    uint32_t pfn = queue_addr / 4096;

    puts(" PFN=0x");
    for (int i = 7; i >= 0; i--)
    {
        int n = (pfn >> (i * 4)) & 0xF;
        putc(n < 10 ? '0' + n : 'A' + n - 10);
    }
    puts("\n");

    /* Tell device where queue is - queue memory should already be initialized! */
    outl(wifi_io_base + VIRTIO_PCI_QUEUE_PFN, pfn);
}

static int wifi_send(network_interface_t *iface, const uint8_t *data, uint32_t len)
{
    (void)iface;
    if (!wifi_initialized || len == 0 || len > PKT_BUF_SIZE - sizeof(virtio_net_hdr_t))
    {
        return -1;
    }

    uint16_t idx = tx_free_idx;
    tx_free_idx = (tx_free_idx + 1) % QUEUE_SIZE;

    /* Prepare buffer with VirtIO net header */
    virtio_net_hdr_t *hdr = (virtio_net_hdr_t *)tx_buffers[idx];
    hdr->flags = 0;
    hdr->gso_type = 0;
    hdr->hdr_len = 0;
    hdr->gso_size = 0;
    hdr->csum_start = 0;
    hdr->csum_offset = 0;

    /* Copy packet data after header */
    uint8_t *pkt_data = tx_buffers[idx] + sizeof(virtio_net_hdr_t);
    for (uint32_t i = 0; i < len; i++)
    {
        pkt_data[i] = data[i];
    }

    /* Setup descriptor */
    tx_desc[idx].addr = (uint64_t)(uint32_t)(&tx_buffers[idx][0]);
    tx_desc[idx].len = sizeof(virtio_net_hdr_t) + len;
    tx_desc[idx].flags = 0;
    tx_desc[idx].next = 0;

    /* Add to available ring - FIX: use our own counter tx_avail_idx */
    uint16_t avail_idx = tx_avail_idx % tx_queue_size;
    tx_avail->ring[avail_idx] = idx;
    __asm__ volatile("mfence" ::: "memory");
    tx_avail_idx++;
    tx_avail->idx = tx_avail_idx;
    __asm__ volatile("mfence" ::: "memory");

    /* Notify device */
    outw(wifi_io_base + VIRTIO_PCI_QUEUE_NOTIFY, TX_QUEUE);

    return len;
}

/* Track the next available ring slot separately */
static uint16_t rx_avail_idx = 0;

static int wifi_recv(network_interface_t *iface, uint8_t *data, uint32_t max_len)
{
    (void)iface;
    if (!wifi_initialized || !data || max_len == 0)
    {
        return 0;
    }

    /* Read ISR to acknowledge any pending interrupts - REQUIRED for VirtIO */
    (void)inb(wifi_io_base + 0x13); /* VIRTIO_PCI_ISR */

    /* Memory barrier before checking used ring */
    __asm__ volatile("mfence" ::: "memory");

    /* Read the used index from device - use volatile pointer to prevent caching */
    volatile virtq_used_t *used_ring = (volatile virtq_used_t *)rx_used;
    uint16_t used_idx = used_ring->idx;

    /* Check if there's data in used ring - compare with wrap-around */
    if ((uint16_t)(used_idx - rx_last_used) == 0)
    {
        return 0; /* No data */
    }

    /* Get used buffer info - use modulo of actual queue size */
    uint16_t ring_idx = rx_last_used % rx_queue_size;
    uint32_t desc_idx = used_ring->ring[ring_idx].id;
    uint32_t total_len = used_ring->ring[ring_idx].len;

    /* Validate desc_idx is within our buffer range */
    if (desc_idx >= QUEUE_SIZE)
    {
        puts("[RX] ERROR: desc_idx out of range!\n");
        rx_last_used++;
        return 0;
    }

    /* Memory barrier after reading */
    __asm__ volatile("mfence" ::: "memory");

    rx_last_used++;

    /* Reinitialize the descriptor for reuse - CRITICAL! */
    rx_desc[desc_idx].addr = (uint64_t)(uint32_t)(&rx_buffers[desc_idx][0]);
    rx_desc[desc_idx].len = PKT_BUF_SIZE;
    rx_desc[desc_idx].flags = VIRTQ_DESC_F_WRITE;
    rx_desc[desc_idx].next = 0;

    /* Skip VirtIO header (10 bytes) */
    if (total_len <= sizeof(virtio_net_hdr_t))
    {
        /* Re-add buffer to available ring */
        volatile virtq_avail_t *avail_ring = (volatile virtq_avail_t *)rx_avail;
        avail_ring->ring[rx_avail_idx % rx_queue_size] = desc_idx;
        __asm__ volatile("mfence" ::: "memory");
        rx_avail_idx++;
        avail_ring->idx = rx_avail_idx;
        __asm__ volatile("mfence" ::: "memory");
        outw(wifi_io_base + VIRTIO_PCI_QUEUE_NOTIFY, RX_QUEUE);
        return 0;
    }

    uint32_t pkt_len = total_len - sizeof(virtio_net_hdr_t);
    if (pkt_len > max_len)
        pkt_len = max_len;

    /* Copy data (skip VirtIO header) */
    uint8_t *src = rx_buffers[desc_idx] + sizeof(virtio_net_hdr_t);
    for (uint32_t i = 0; i < pkt_len; i++)
    {
        data[i] = src[i];
    }

    /* Re-add buffer to available ring for reuse */
    volatile virtq_avail_t *avail_ring = (volatile virtq_avail_t *)rx_avail;
    avail_ring->ring[rx_avail_idx % rx_queue_size] = desc_idx;
    __asm__ volatile("mfence" ::: "memory");
    rx_avail_idx++;
    avail_ring->idx = rx_avail_idx;
    __asm__ volatile("mfence" ::: "memory");

    /* Notify device that buffer is available again */
    outw(wifi_io_base + VIRTIO_PCI_QUEUE_NOTIFY, RX_QUEUE);

    return pkt_len;
}

/* Debug function to show RX queue state */
int debug_rx_state(void)
{
    if (!wifi_initialized)
    {
        puts("[DBG] WiFi not initialized!\n");
        return -1;
    }

    volatile virtq_used_t *used_ring = (volatile virtq_used_t *)rx_used;
    volatile virtq_avail_t *avail_ring = (volatile virtq_avail_t *)rx_avail;

    puts("[DBG] RX used_idx=");
    uint16_t u = used_ring->idx;
    putc('0' + (u / 100) % 10);
    putc('0' + (u / 10) % 10);
    putc('0' + u % 10);
    puts(" last_used=");
    putc('0' + (rx_last_used / 100) % 10);
    putc('0' + (rx_last_used / 10) % 10);
    putc('0' + rx_last_used % 10);
    puts(" avail_idx=");
    uint16_t a = avail_ring->idx;
    putc('0' + (a / 100) % 10);
    putc('0' + (a / 10) % 10);
    putc('0' + a % 10);
    puts("\n");

    return 0;
}

__attribute__((weak)) int wifi_driver_init(void)
{
    puts("[NET] Initializing VirtIO-Net driver...\n");

    wifi_io_base = find_virtio_net();
    if (wifi_io_base == 0)
    {
        puts("[NET] ERROR: VirtIO-Net device not found!\n");
        return -1;
    }

    puts("[NET] Found device at I/O base 0x");
    for (int i = 3; i >= 0; i--)
    {
        int n = (wifi_io_base >> (i * 4)) & 0xF;
        putc(n < 10 ? '0' + n : 'A' + n - 10);
    }
    puts("\n");

    /* Reset device */
    outb(wifi_io_base + VIRTIO_PCI_STATUS, 0);

    /* Acknowledge device */
    outb(wifi_io_base + VIRTIO_PCI_STATUS, 1);     /* ACKNOWLEDGE */
    outb(wifi_io_base + VIRTIO_PCI_STATUS, 1 | 2); /* + DRIVER */

    /* Negotiate features (just accept basic) */
    uint32_t features = inl(wifi_io_base + VIRTIO_PCI_HOST_FEATURES);
    features &= (1 << 5); /* VIRTIO_NET_F_MAC */
    outl(wifi_io_base + VIRTIO_PCI_GUEST_FEATURES, features);

    /* Features OK */
    outb(wifi_io_base + VIRTIO_PCI_STATUS, 1 | 2 | 8); /* + FEATURES_OK */

    /* Read MAC address */
    for (int i = 0; i < 6; i++)
    {
        wifi_mac[i] = inb(wifi_io_base + VIRTIO_PCI_CONFIG + i);
    }
    puts("[NET] MAC: ");
    for (int i = 0; i < 6; i++)
    {
        putc("0123456789ABCDEF"[(wifi_mac[i] >> 4) & 0xF]);
        putc("0123456789ABCDEF"[wifi_mac[i] & 0xF]);
        if (i < 5)
            putc(':');
    }
    puts("\n");

    puts("[NET] Setting up RX queue memory...\n");

    /* Zero queue memory FIRST - use faster word-sized writes */
    {
        uint32_t *p = (uint32_t *)rx_queue_mem;
        for (int i = 0; i < 16384 / 4; i++)
            p[i] = 0;
        p = (uint32_t *)tx_queue_mem;
        for (int i = 0; i < 16384 / 4; i++)
            p[i] = 0;
    }

    /* Note: Buffer memory doesn't need zeroing - it will be overwritten on use */

    /* Initialize RX descriptors and buffers BEFORE telling device about queue */
    for (int i = 0; i < QUEUE_SIZE; i++)
    {
        rx_desc[i].addr = (uint64_t)(uint32_t)(&rx_buffers[i][0]);
        rx_desc[i].len = PKT_BUF_SIZE;
        rx_desc[i].flags = VIRTQ_DESC_F_WRITE; /* Device writes to this buffer */
        rx_desc[i].next = 0;

        /* Add to available ring */
        rx_avail->ring[i] = i;
    }
    rx_avail->flags = 0;        /* No interrupt suppression - allow device to update used ring */
    rx_avail->idx = QUEUE_SIZE; /* We've added QUEUE_SIZE buffers */
    rx_avail_idx = QUEUE_SIZE;  /* Track our next available slot */

    /* Initialize used ring tracking - CRITICAL: start at 0, device will increment */
    rx_last_used = 0;
    /* NOTE: Do NOT write to rx_used - that's the device's ring!
     * The device will write to used ring when it has processed buffers.
     * We already zeroed the memory above, so used->idx starts at 0. */

    puts("[NET] Setting up TX queue memory...\n");

    /* Initialize TX descriptors */
    for (int i = 0; i < QUEUE_SIZE; i++)
    {
        tx_desc[i].addr = 0;
        tx_desc[i].len = 0;
        tx_desc[i].flags = 0; /* Device reads from this buffer */
        tx_desc[i].next = 0;
    }
    tx_avail->flags = 0;
    tx_avail->idx = 0;
    tx_last_used = 0;
    tx_free_idx = 0;
    tx_avail_idx = 0;

    /* Memory barrier to ensure all writes are visible */
    __asm__ volatile("mfence" ::: "memory");

    /* Now tell device where queues are - queue must be ready before this! */
    setup_virtqueue(RX_QUEUE, rx_queue_mem, &rx_queue_size);
    setup_virtqueue(TX_QUEUE, tx_queue_mem, &tx_queue_size);

    /* Driver ready */
    outb(wifi_io_base + VIRTIO_PCI_STATUS, 1 | 2 | 4 | 8); /* + DRIVER_OK */

    puts("[NET] Notifying RX queue...\n");

    /* Notify RX queue that buffers are available */
    outw(wifi_io_base + VIRTIO_PCI_QUEUE_NOTIFY, RX_QUEUE);

    /* Setup network interface */
    net_iface.name[0] = 'e';
    net_iface.name[1] = 't';
    net_iface.name[2] = 'h';
    net_iface.name[3] = '0';
    net_iface.name[4] = '\0';
    for (int i = 0; i < 6; i++)
        net_iface.mac_addr[i] = wifi_mac[i];
    net_iface.link_up = true;
    net_iface.send_packet = wifi_send;
    net_iface.recv_packet = wifi_recv;

    extern int netif_register(network_interface_t * iface);
    netif_register(&net_iface);

    wifi_initialized = true;
    puts("[NET] Driver initialized successfully!\n");
    return 0;
}

__attribute__((weak)) int wifi_driver_test(void)
{
    puts("\n=== WiFi Driver Test ===\n");
    if (!wifi_initialized && wifi_driver_init() != 0)
    {
        puts("FAILED\n");
        return -1;
    }
    puts("Status: CONNECTED\nMAC: ");
    for (int i = 0; i < 6; i++)
    {
        putc("0123456789ABCDEF"[(wifi_mac[i] >> 4) & 0xF]);
        putc("0123456789ABCDEF"[wifi_mac[i] & 0xF]);
        if (i < 5)
            putc(':');
    }
    puts("\n=== Test Complete ===\n");
    return 0;
}

__attribute__((weak)) int wifi_get_mac(uint8_t *mac)
{
    if (!mac)
        return -1;
    for (int i = 0; i < 6; i++)
        mac[i] = wifi_mac[i];
    return 0;
}

__attribute__((weak)) int wifi_is_connected(void) { return wifi_initialized ? 1 : 0; }
__attribute__((weak)) void wifi_driver_shutdown(void) { wifi_initialized = false; }

/* GPU Driver (VGA Mode 13h) */

#define VGA_WIDTH 320
#define VGA_HEIGHT 200
/* VGA Memory Logic for Double Buffering */
#define VGA_HW_MEM ((uint8_t *)0xA0000)
static uint8_t *vga_target = (uint8_t *)0xA0000;

/* Allow redirecting VGA drawing to a backbuffer */
__attribute__((weak)) void vga_set_target(uint8_t *ptr)
{
    vga_target = ptr ? ptr : (uint8_t *)0xA0000;
}

#define VGA_MEM vga_target

static bool gpu_initialized = false;
static bool in_graphics_mode = false;

/* VirtIO-GPU constants for disabling scanout */
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_RESP_OK_NODATA 0x1100

/* VirtIO-GPU state for cleanup */
static uint16_t virtio_gpu_io_base = 0;
static bool virtio_gpu_found = false;

/* Find VirtIO-GPU device */
static uint16_t find_virtio_gpu(void)
{
    for (int bus = 0; bus < 256; bus++)
    {
        for (int dev = 0; dev < 32; dev++)
        {
            uint32_t id = pci_read(bus, dev, 0, 0);
            if ((id & 0xFFFF) == VIRTIO_VENDOR)
            {
                uint16_t did = (id >> 16) & 0xFFFF;
                /* VirtIO-GPU device IDs: 0x1050 (legacy) or 0x1040+16 */
                if (did == 0x1050 || did == 0x1040)
                {
                    uint32_t bar0 = pci_read(bus, dev, 0, 0x10);
                    if (bar0 & 1)
                    {
                        virtio_gpu_found = true;
                        return (bar0 & 0xFFFC);
                    }
                }
            }
        }
    }
    return 0;
}

/* Disable VirtIO-GPU scanout to allow VGA text mode to display */
static void disable_virtio_gpu_scanout(void)
{
    if (!virtio_gpu_found && virtio_gpu_io_base == 0)
    {
        virtio_gpu_io_base = find_virtio_gpu();
    }

    if (virtio_gpu_io_base == 0)
    {
        return; /* No VirtIO-GPU, nothing to disable */
    }

    /*
     * To properly disable VirtIO-GPU scanout, we would need to send a
     * SET_SCANOUT command with resource_id=0. However, this requires
     * the full VirtIO queue setup. For now, we'll just reset the device
     * which will disable the scanout.
    */

    /* Reset VirtIO-GPU device by writing 0 to status register */
    outb(virtio_gpu_io_base + VIRTIO_PCI_STATUS, 0);

    /* Small delay for reset to take effect */
    for (volatile int i = 0; i < 10000; i++)
        ;
}

/* Set VGA Mode 13h (320x200x256 colors) via BIOS int 10h simulation */
void set_mode_13h(void)
{
    /* Write to VGA registers directly for mode 13h */
    /* Miscellaneous Output Register */
    outb(0x3C2, 0x63);
    outb(0x80, 0);

    /* Sequencer registers */
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03); /* Reset */
    outb(0x80, 0);
    outb(0x3C4, 0x01);
    outb(0x3C5, 0x01); /* Clocking Mode */
    outb(0x80, 0);
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x0F); /* Map Mask */
    outb(0x80, 0);
    outb(0x3C4, 0x03);
    outb(0x3C5, 0x00); /* Character Map */
    outb(0x80, 0);
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x0E); /* Memory Mode */
    outb(0x80, 0);

    /* Unlock CRTC */
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);
    outb(0x80, 0);

    /* CRTC registers for 320x200 */
    static const uint8_t crtc[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF};
    for (int i = 0; i < 25; i++)
    {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
        outb(0x80, 0);
    }

    /* Graphics Controller */
    static const uint8_t gc[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF};
    for (int i = 0; i < 9; i++)
    {
        outb(0x3CE, i);
        outb(0x3CF, gc[i]);
        outb(0x80, 0);
    }

    /* Attribute Controller */
    inb(0x3DA); /* Reset flip-flop */
    static const uint8_t ac[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41, 0x00, 0x0F, 0x00, 0x00};
    for (int i = 0; i < 21; i++)
    {
        inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, ac[i]);
        outb(0x80, 0);
    }
    inb(0x3DA);
    outb(0x3C0, 0x20); /* Enable display */

    in_graphics_mode = true;
}

/* Set text mode 3 (80x25) - use kernel's vga_set_mode function */
extern void vga_set_text_mode(void);

static void set_text_mode(void)
{
    /* First, disable VirtIO-GPU scanout if present */
    disable_virtio_gpu_scanout();

    /* Use a more reliable approach - reset VGA to known state */

    /* Miscellaneous Output Register - select 80x25 timing */
    outb(0x3C2, 0x67);

    /* Reset sequencer */
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x01); /* Sync reset */
    outb(0x3C4, 0x01);
    outb(0x3C5, 0x00); /* Clocking: 9 dot, no shift */
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x03); /* Map Mask: planes 0,1 */
    outb(0x3C4, 0x03);
    outb(0x3C5, 0x00); /* Char Map Select */
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x02); /* Memory Mode: O/E, !chain4 */
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03); /* End reset */

    /* Unlock CRTC */
    outb(0x3D4, 0x11);
    uint8_t v = inb(0x3D5);
    outb(0x3D5, v & 0x7F);

    /* CRTC registers for 80x25 16-color text (mode 3) */
    static const uint8_t crtc[] = {
        0x5F, /* 0: Horizontal Total */
        0x4F, /* 1: Horizontal Display End */
        0x50, /* 2: Start Horizontal Blanking */
        0x82, /* 3: End Horizontal Blanking */
        0x55, /* 4: Start Horizontal Retrace */
        0x81, /* 5: End Horizontal Retrace */
        0xBF, /* 6: Vertical Total */
        0x1F, /* 7: Overflow */
        0x00, /* 8: Preset Row Scan */
        0x4F, /* 9: Max Scan Line (16 high chars) */
        0x0D, /* A: Cursor Start */
        0x0E, /* B: Cursor End */
        0x00, /* C: Start Address High */
        0x00, /* D: Start Address Low */
        0x00, /* E: Cursor Location High */
        0x00, /* F: Cursor Location Low */
        0x9C, /* 10: Vertical Retrace Start */
        0x8E, /* 11: Vertical Retrace End (and protect) */
        0x8F, /* 12: Vertical Display End */
        0x28, /* 13: Offset (80 chars / 2) */
        0x1F, /* 14: Underline Location */
        0x96, /* 15: Start Vertical Blanking */
        0xB9, /* 16: End Vertical Blanking */
        0xA3, /* 17: Mode Control */
        0xFF  /* 18: Line Compare */
    };
    for (int i = 0; i < 25; i++)
    {
        outb(0x3D4, i);
        outb(0x3D5, crtc[i]);
    }

    /* Graphics Controller */
    outb(0x3CE, 0x00);
    outb(0x3CF, 0x00); /* Set/Reset */
    outb(0x3CE, 0x01);
    outb(0x3CF, 0x00); /* Enable Set/Reset */
    outb(0x3CE, 0x02);
    outb(0x3CF, 0x00); /* Color Compare */
    outb(0x3CE, 0x03);
    outb(0x3CF, 0x00); /* Data Rotate */
    outb(0x3CE, 0x04);
    outb(0x3CF, 0x00); /* Read Map Select */
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x10); /* Mode: Odd/Even */
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x0E); /* Misc: text, B8000, O/E */
    outb(0x3CE, 0x07);
    outb(0x3CF, 0x00); /* Color Don't Care */
    outb(0x3CE, 0x08);
    outb(0x3CF, 0xFF); /* Bit Mask */

    /* Attribute Controller */
    inb(0x3DA); /* Reset flip-flop */

    /* Palette entries 0-15: standard CGA colors */
    static const uint8_t pal[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
                                  0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F};
    for (int i = 0; i < 16; i++)
    {
        inb(0x3DA);
        outb(0x3C0, i);
        outb(0x3C0, pal[i]);
    }
    /* Attribute mode control */
    inb(0x3DA);
    outb(0x3C0, 0x10);
    outb(0x3C0, 0x0C);
    inb(0x3DA);
    outb(0x3C0, 0x11);
    outb(0x3C0, 0x00); /* Overscan */
    inb(0x3DA);
    outb(0x3C0, 0x12);
    outb(0x3C0, 0x0F); /* Color Plane Enable */
    inb(0x3DA);
    outb(0x3C0, 0x13);
    outb(0x3C0, 0x08); /* Horiz Pixel Pan */
    inb(0x3DA);
    outb(0x3C0, 0x14);
    outb(0x3C0, 0x00); /* Color Select */

    /* Enable display */
    inb(0x3DA);
    outb(0x3C0, 0x20);

    /* Clear text mode screen memory */
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < 80 * 25; i++)
    {
        vga[i] = 0x0720; /* Gray on black space */
    }

    in_graphics_mode = false;
}

/* Weak alias for disabling scanout (restoring text mode) */
__attribute__((weak)) int gpu_disable_scanout(void)
{
    set_text_mode();
    return 0;
}

/* Set palette color */
static void set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    outb(0x3C8, idx);
    outb(0x3C9, r >> 2);
    outb(0x3C9, g >> 2);
    outb(0x3C9, b >> 2);
}

/* Setup a nice 256-color palette */
void setup_palette(void)
{
    /* Standard 16 colors */
    set_palette(0, 0, 0, 0);        /* Black */
    set_palette(1, 0, 0, 170);      /* Blue */
    set_palette(2, 0, 170, 0);      /* Green */
    set_palette(3, 0, 170, 170);    /* Cyan */
    set_palette(4, 170, 0, 0);      /* Red */
    set_palette(5, 170, 0, 170);    /* Magenta */
    set_palette(6, 170, 85, 0);     /* Brown */
    set_palette(7, 170, 170, 170);  /* Light Gray */
    set_palette(8, 85, 85, 85);     /* Dark Gray */
    set_palette(9, 85, 85, 255);    /* Light Blue */
    set_palette(10, 85, 255, 85);   /* Light Green */
    set_palette(11, 85, 255, 255);  /* Light Cyan */
    set_palette(12, 255, 85, 85);   /* Light Red */
    set_palette(13, 255, 85, 255);  /* Light Magenta */
    set_palette(14, 255, 255, 85);  /* Yellow */
    set_palette(15, 255, 255, 255); /* White */

    /* Grayscale ramp 16-31 */
    for (int i = 0; i < 16; i++)
    {
        uint8_t v = i * 17;
        set_palette(16 + i, v, v, v);
    }

    /* Color cube 32-255 */
    int idx = 32;
    for (int r = 0; r < 6 && idx < 256; r++)
    {
        for (int g = 0; g < 6 && idx < 256; g++)
        {
            for (int b = 0; b < 6 && idx < 256; b++)
            {
                set_palette(idx++, r * 51, g * 51, b * 51);
            }
        }
    }
}

__attribute__((weak)) int gpu_driver_init(void)
{
    puts("[GPU] Initializing VGA driver...\n");
    gpu_initialized = true;
    puts("[GPU] Driver ready (VGA 320x200x256)\n");
    return 0;
}

__attribute__((weak)) uint32_t *gpu_setup_framebuffer(void)
{
    if (!gpu_initialized)
        gpu_driver_init();
    set_mode_13h();
    setup_palette();
    return (uint32_t *)VGA_MEM;
}

__attribute__((weak)) int gpu_flush(void)
{
    /* Copy backbuffer to hardware if buffering is active */
    if (vga_target != VGA_HW_MEM)
    {
        uint8_t *hw = VGA_HW_MEM;
        /* Simple copy loop (memcpy not available in stubs) */
        for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        {
            hw[i] = vga_target[i];
        }
    }
    return 0;
}

__attribute__((weak)) int gpu_get_width(void) { return VGA_WIDTH; }
__attribute__((weak)) int gpu_get_height(void) { return VGA_HEIGHT; }

/* Color mapping helper */
static uint8_t rgb_to_vga(uint32_t c)
{
    /* If already an index (0-255), return it */
    if (c < 256)
        return (uint8_t)c;

    /* Map RGB values to standard VGA palette indices */
    switch (c)
    {
    case 0x000000:
        return 0;
    case 0x0000AA:
        return 1;
    case 0x00AA00:
        return 2;
    case 0x00AAAA:
        return 3;
    case 0xAA0000:
        return 4;
    case 0xAA00AA:
        return 5;
    case 0xAA5500:
        return 6;
    case 0xAAAAAA:
        return 7;
    case 0x555555:
        return 8;
    case 0x5555FF:
        return 9;
    case 0x55FF55:
        return 10;
    case 0x55FFFF:
        return 11;
    case 0xFF5555:
        return 12;
    case 0xFF55FF:
        return 13;
    case 0xFFFF55:
        return 14;
    case 0xFFFFFF:
        return 15;
    default:
        return 15;
    }
}

/* VGA Drawing Primitives - Exposed for fallback */
void vga_clear(uint32_t color)
{
    uint8_t c = rgb_to_vga(color);
    uint8_t *vga = VGA_MEM;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
    {
        vga[i] = c;
    }
}

void vga_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    uint8_t c = rgb_to_vga(color);
    uint8_t *vga = VGA_MEM;
    for (int py = y; py < y + h && py < VGA_HEIGHT; py++)
    {
        if (py < 0)
            continue;
        for (int px = x; px < x + w && px < VGA_WIDTH; px++)
        {
            if (px < 0)
                continue;
            vga[py * VGA_WIDTH + px] = c;
        }
    }
}

void vga_draw_pixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= VGA_WIDTH || y >= VGA_HEIGHT)
        return;
    VGA_MEM[y * VGA_WIDTH + x] = rgb_to_vga(color);
}

/* Weak aliases for default GPU driver interface */
__attribute__((weak)) void gpu_clear(uint32_t color) { vga_clear(color); }
__attribute__((weak)) void gpu_fill_rect(int x, int y, int w, int h, uint32_t c) { vga_fill_rect(x, y, w, h, c); }
__attribute__((weak)) void gpu_draw_pixel(int x, int y, uint32_t c) { vga_draw_pixel(x, y, c); }

/* Text drawing needs font... assuming 8x8 font is available or simple implementation */
/* For now, just a stub for complex text if font missing, or use BIOS font in Real Mode (impossible here) */
/* We'll use a simple block character for testing if font not found */
/* Actually, we need to implement vga_draw_char/string properly for GUI */
extern const uint8_t font8x8_basic[]; /* We'll need to provide this or assuming it exists */
/* Since we don't have font easily, we'll SKIP char/string renaming for now and rely on gui_apps fallback to draw_pixel */
/* Wait, gui_apps uses gpu_draw_char. If that fails, no text. */
/* I will add vga_draw_char using draw_pixel in gui_apps.c instead of here, to avoid font dependency issues in stubs */

/* Simple 8x8 font - uppercase, lowercase, numbers, symbols */
static const uint8_t font8x8[128][8] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x18, 0x00},
    ['"'] = {0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['\''] = {0x18, 0x18, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['('] = {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    [')'] = {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    [','] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
    ['-'] = {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    ['/'] = {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00},
    [':'] = {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    ['?'] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00},
    ['_'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    /* Numbers */
    ['0'] = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00},
    ['1'] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    ['2'] = {0x3C, 0x66, 0x06, 0x1C, 0x30, 0x60, 0x7E, 0x00},
    ['3'] = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00},
    ['4'] = {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00},
    ['5'] = {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00},
    ['6'] = {0x3C, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['7'] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    ['8'] = {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00},
    ['9'] = {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00},
    /* Uppercase */
    ['A'] = {0x18, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    ['B'] = {0x7C, 0x66, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00},
    ['C'] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
    ['D'] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00},
    ['E'] = {0x7E, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x7E, 0x00},
    ['F'] = {0x7E, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x00},
    ['G'] = {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00},
    ['H'] = {0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x66, 0x00},
    ['I'] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['J'] = {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00},
    ['K'] = {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00},
    ['L'] = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00},
    ['M'] = {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00},
    ['N'] = {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00},
    ['O'] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['P'] = {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00},
    ['Q'] = {0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00},
    ['R'] = {0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00},
    ['S'] = {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00},
    ['T'] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    ['U'] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['V'] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    ['W'] = {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    ['X'] = {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00},
    ['Y'] = {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
    ['Z'] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00},
    /* Lowercase */
    ['a'] = {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00},
    ['b'] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00},
    ['c'] = {0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00},
    ['d'] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},
    ['e'] = {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
    ['f'] = {0x1C, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00},
    ['g'] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    ['h'] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    ['i'] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['j'] = {0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38},
    ['k'] = {0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00},
    ['l'] = {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    ['m'] = {0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    ['n'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    ['o'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
    ['p'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60},
    ['q'] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06},
    ['r'] = {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00},
    ['s'] = {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00},
    ['t'] = {0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00},
    ['u'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00},
    ['v'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    ['w'] = {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    ['x'] = {0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00},
    ['y'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    ['z'] = {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00},
};

void vga_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg)
{
    if (c > 127)
        c = '?';

    /* Use font table if available, otherwise skip (avoid crash) */
    /* Assumes font8x8 is defined in this file */

    /* Map colors correctly */
    uint8_t fgc = rgb_to_vga(fg);
    uint8_t bgc = rgb_to_vga(bg);

    const uint8_t *glyph = font8x8[c];
    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            if (x + col < VGA_WIDTH && y + row < VGA_HEIGHT)
            {
                uint8_t color = (glyph[row] & (0x80 >> col)) ? fgc : bgc;
                /* Write directly to memory to avoid double-mapping check overhead */
                VGA_MEM[(y + row) * VGA_WIDTH + (x + col)] = color;
            }
        }
    }
}

void vga_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg)
{
    if (!str)
        return;
    int cx = x;
    while (*str)
    {
        vga_draw_char(cx, y, *str, fg, bg);
        cx += 8;
        str++;
    }
}

__attribute__((weak)) void gpu_draw_char(int x, int y, uint8_t c, uint32_t fg, uint32_t bg) { vga_draw_char(x, y, c, fg, bg); }
__attribute__((weak)) void gpu_draw_string(int x, int y, const uint8_t *str, uint32_t fg, uint32_t bg) { vga_draw_string(x, y, str, fg, bg); }

/* GUI state */
static char gui_input_buf[64];
static int gui_input_len = 0;

/* Screen backup for GUI exit via reboot */
#define VGA_TEXT_MEM ((volatile uint16_t *)0xB8000)
#define SCREEN_BACKUP_ADDR ((volatile uint16_t *)0x90000)   /* Safe area in low memory */
#define GUI_REBOOT_FLAG_ADDR ((volatile uint32_t *)0x9F000) /* Flag location */
#define GUI_CURSOR_ROW_ADDR ((volatile uint32_t *)0x9F004)  /* Cursor row backup */
#define GUI_CURSOR_COL_ADDR ((volatile uint32_t *)0x9F008)  /* Cursor col backup */
#define GUI_REBOOT_MAGIC 0x47554952                         /* "GUIR" - GUI Reboot magic */

/* External cursor variables from io.asm */
extern uint32_t cursor_row;
extern uint32_t cursor_col;

/* External function to update hardware cursor */
extern void set_cursor_hardware(void);

/* Save current text screen before entering GUI */
static void save_text_screen(void)
{
    /* Save 80x25 = 2000 characters (4000 bytes) */
    for (int i = 0; i < 80 * 25; i++)
    {
        SCREEN_BACKUP_ADDR[i] = VGA_TEXT_MEM[i];
    }
    /* Save cursor position */
    *GUI_CURSOR_ROW_ADDR = cursor_row;
    *GUI_CURSOR_COL_ADDR = cursor_col;
    /* Set the reboot flag */
    *GUI_REBOOT_FLAG_ADDR = GUI_REBOOT_MAGIC;
}

/* Restore text screen after reboot (called from shell init) */
int gui_check_and_restore_screen(void)
{
    if (*GUI_REBOOT_FLAG_ADDR == GUI_REBOOT_MAGIC)
    {
        /* Clear the flag first */
        *GUI_REBOOT_FLAG_ADDR = 0;

        /* Restore the screen exactly as it was */
        for (int i = 0; i < 80 * 25; i++)
        {
            VGA_TEXT_MEM[i] = SCREEN_BACKUP_ADDR[i];
        }

        /* Restore cursor position */
        cursor_row = *GUI_CURSOR_ROW_ADDR;
        cursor_col = *GUI_CURSOR_COL_ADDR;

        /* Update hardware cursor to match */
        set_cursor_hardware();

        return 1; /* Restored */
    }
    return 0; /* Normal boot */
}

/* Draw GUI input box */
static void gui_draw_input_box(void)
{
    /* Input box background - black border, dark blue inside (same as screen) */
    gpu_fill_rect(10, 175, 300, 20, 0); /* Black border */
    gpu_fill_rect(12, 177, 296, 16, 1); /* Dark blue inside (matches background) */

    /* Draw current input text in YELLOW (14) on dark blue (1) - same as title */
    for (int i = 0; i < gui_input_len && i < 35; i++)
    {
        gpu_draw_char(15 + i * 8, 179, gui_input_buf[i], 14, 1);
    }

    /* Draw cursor - yellow block */
    gpu_fill_rect(15 + gui_input_len * 8, 179, 7, 10, 14);
}

/* External reboot function */
extern void sys_reboot(void);

__attribute__((weak)) int gpu_driver_test(void)
{
    /* Save the current text screen FIRST - before any output */
    save_text_screen();

    gpu_setup_framebuffer();

    /* Reset input state */
    gui_input_len = 0;

    /* Clear to dark blue (color 1) */
    gpu_clear(1);

    /* Draw colored rectangles */
    gpu_fill_rect(20, 25, 80, 50, 4);  /* Red */
    gpu_fill_rect(120, 25, 80, 50, 2); /* Green */
    gpu_fill_rect(220, 25, 80, 50, 9); /* Light Blue */

    gpu_fill_rect(20, 95, 80, 50, 14); /* Yellow */
    gpu_fill_rect(120, 95, 80, 50, 5); /* Magenta */
    gpu_fill_rect(220, 95, 80, 50, 3); /* Cyan */

    /* Draw title in YELLOW (centered: 320/2 - 10chars*8/2 = 160-40 = 120) */
    gpu_draw_string(120, 5, (uint8_t *)"AurionOS GUI", 14, 1);
    /* Draw ESC instruction centered: "ESC TO EXIT" = 11 chars, 320/2 - 11*8/2 = 116 */
    gpu_draw_string(116, 160, (uint8_t *)"ESC TO EXIT", 14, 1);

    /* Draw input box */
    gui_draw_input_box();

    /* Use kernel's getkey function which works with interrupt handler */
    extern uint16_t c_getkey(void);

    /* Main GUI loop - use kernel getkey */
    while (1)
    {
        uint16_t key = c_getkey();
        uint8_t ascii = key & 0xFF;
        uint8_t scan = (key >> 8) & 0xFF;

        /* ESC - check both ASCII 27 and scancode 0x01 */
        if (ascii == 27 || scan == 0x01)
        {
            /* Reboot to restore text mode - screen will be restored on boot */
            sys_reboot();
            /* Should not reach here, but just in case */
            while (1)
                ;
        }

        /* Backspace */
        if (ascii == 8 || scan == 0x0E)
        {
            if (gui_input_len > 0)
            {
                gui_input_len--;
                gui_draw_input_box();
            }
            continue;
        }

        /* Enter - clear input */
        if (ascii == 13 || ascii == 10)
        {
            gui_input_len = 0;
            gui_draw_input_box();
            continue;
        }

        /* Printable character */
        if (ascii >= 32 && ascii < 127 && gui_input_len < 35)
        {
            gui_input_buf[gui_input_len++] = ascii;
            gui_draw_input_box();
        }
    }

    /* Should never reach here */
    return 0;
}
