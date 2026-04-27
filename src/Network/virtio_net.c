/*
 * VirtIO-Net (legacy PCI) - Ethernet for QEMU and similar VMs.
 * This is not Wi-Fi; 802.11 hardware is handled in wifi_pci.c / future chipset drivers.
*/

#include <stdint.h>
#include <stdbool.h>
#include "../../include/network.h"

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

int virtio_net_driver_init(void)
{
    if (wifi_initialized) {
        puts("[NET] VirtIO-Net already initialized.\n");
        return 0;
    }

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

int virtio_net_ready(void) { return wifi_initialized ? 1 : 0; }

void virtio_net_get_mac(uint8_t *mac) {
    if (!mac) return;
    for (int i = 0; i < 6; i++) mac[i] = wifi_mac[i];
}

void virtio_net_shutdown_impl(void) { wifi_initialized = false; }
