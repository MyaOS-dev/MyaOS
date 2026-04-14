#include "rtl8169.h"
#include "device.h"
#include "pci.h"
#include <stddef.h>

#define RTL8169_VENDOR_REALTEK 0x10ECu

#define RTL8169_DEV_8168 0x8168u
#define RTL8169_DEV_8169 0x8169u

#define RTL8169_REG_IDR0 0x00u
#define RTL8169_REG_TNPDS_LO 0x20u
#define RTL8169_REG_TNPDS_HI 0x24u
#define RTL8169_REG_CR 0x37u
#define RTL8169_REG_TPPOLL 0x38u
#define RTL8169_REG_IMR 0x3Cu
#define RTL8169_REG_ISR 0x3Eu
#define RTL8169_REG_TCR 0x40u
#define RTL8169_REG_RCR 0x44u
#define RTL8169_REG_9346CR 0x50u
#define RTL8169_REG_RMS 0xDAu
#define RTL8169_REG_CPLUSCMD 0xE0u
#define RTL8169_REG_RDSAR_LO 0xE4u
#define RTL8169_REG_RDSAR_HI 0xE8u

#define RTL8169_CR_TE 0x04u
#define RTL8169_CR_RE 0x08u
#define RTL8169_CR_RST 0x10u

#define RTL8169_TPPOLL_NPQ 0x40u

#define RTL8169_DESC_OWN 0x80000000u
#define RTL8169_DESC_EOR 0x40000000u
#define RTL8169_DESC_FS 0x20000000u
#define RTL8169_DESC_LS 0x10000000u
#define RTL8169_DESC_LEN_MASK 0x00003FFFu

#define RTL8169_RX_ACCEPT_ALL_PHYS 0x00000001u
#define RTL8169_RX_ACCEPT_MY_PHYS 0x00000002u
#define RTL8169_RX_ACCEPT_MCAST 0x00000004u
#define RTL8169_RX_ACCEPT_BCAST 0x00000008u

#define RTL8169_TX_DESC_COUNT 16u
#define RTL8169_RX_DESC_COUNT 16u
#define RTL8169_TX_BUF_SIZE 2048u
#define RTL8169_RX_BUF_SIZE 2048u
#define RTL8169_TX_TIMEOUT_LOOPS 300000u

typedef struct __attribute__((packed)) {
    uint32_t opts1;
    uint32_t opts2;
    uint64_t addr;
} rtl8169_desc_t;

typedef struct {
    uint8_t ready;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint8_t access_mode;
    uint8_t reserved1;
    uint16_t io_base;
    uint16_t device_id;
    uint16_t reserved2;
    volatile uint8_t* mmio_base;
    uint8_t mac[6];
    uint8_t reserved0[2];
    uint16_t tx_next;
    uint16_t rx_next;
} rtl8169_state_t;

static rtl8169_state_t g_rtl8169;
static rtl8169_desc_t g_tx_desc[RTL8169_TX_DESC_COUNT] __attribute__((aligned(256)));
static rtl8169_desc_t g_rx_desc[RTL8169_RX_DESC_COUNT] __attribute__((aligned(256)));
static uint8_t g_tx_buf[RTL8169_TX_DESC_COUNT][RTL8169_TX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t g_rx_buf[RTL8169_RX_DESC_COUNT][RTL8169_RX_BUF_SIZE] __attribute__((aligned(16)));

static const uint16_t g_supported_devices[] = {
    RTL8169_DEV_8168,
    RTL8169_DEV_8169,
};

#define RTL8169_ACCESS_NONE 0u
#define RTL8169_ACCESS_IO 1u
#define RTL8169_ACCESS_MMIO 2u

static void mem_zero(void* ptr, size_t size) {
    uint8_t* out = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        out[i] = 0u;
    }
}

static void mem_copy(void* dst, const void* src, size_t size) {
    uint8_t* out = (uint8_t*)dst;
    const uint8_t* in = (const uint8_t*)src;
    for (size_t i = 0; i < size; i++) {
        out[i] = in[i];
    }
}

static void cpu_relax(void) {
    __asm__ __volatile__("pause");
}

static void barrier(void) {
    __sync_synchronize();
}

static inline void io_out8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_out16(uint16_t port, uint16_t value) {
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_out32(uint16_t port, uint32_t value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t io_in8(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t io_in16(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint32_t io_in32(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t rtl_read8(uint16_t reg) {
    if (g_rtl8169.access_mode == RTL8169_ACCESS_IO) {
        return io_in8((uint16_t)(g_rtl8169.io_base + reg));
    }
    if (g_rtl8169.access_mode == RTL8169_ACCESS_MMIO && g_rtl8169.mmio_base) {
        volatile uint8_t* ptr = (volatile uint8_t*)(uintptr_t)(g_rtl8169.mmio_base + reg);
        return *ptr;
    }
    return 0u;
}

static uint16_t rtl_read16(uint16_t reg) {
    if (g_rtl8169.access_mode == RTL8169_ACCESS_IO) {
        return io_in16((uint16_t)(g_rtl8169.io_base + reg));
    }
    if (g_rtl8169.access_mode == RTL8169_ACCESS_MMIO && g_rtl8169.mmio_base) {
        volatile uint16_t* ptr = (volatile uint16_t*)(uintptr_t)(g_rtl8169.mmio_base + reg);
        return *ptr;
    }
    return 0u;
}

static uint32_t rtl_read32(uint16_t reg) {
    if (g_rtl8169.access_mode == RTL8169_ACCESS_IO) {
        return io_in32((uint16_t)(g_rtl8169.io_base + reg));
    }
    if (g_rtl8169.access_mode == RTL8169_ACCESS_MMIO && g_rtl8169.mmio_base) {
        volatile uint32_t* ptr = (volatile uint32_t*)(uintptr_t)(g_rtl8169.mmio_base + reg);
        return *ptr;
    }
    return 0u;
}

static void rtl_write8(uint16_t reg, uint8_t value) {
    if (g_rtl8169.access_mode == RTL8169_ACCESS_IO) {
        io_out8((uint16_t)(g_rtl8169.io_base + reg), value);
        return;
    }
    if (g_rtl8169.access_mode == RTL8169_ACCESS_MMIO && g_rtl8169.mmio_base) {
        volatile uint8_t* ptr = (volatile uint8_t*)(uintptr_t)(g_rtl8169.mmio_base + reg);
        *ptr = value;
        (void)rtl_read8(reg);
    }
}

static void rtl_write16(uint16_t reg, uint16_t value) {
    if (g_rtl8169.access_mode == RTL8169_ACCESS_IO) {
        io_out16((uint16_t)(g_rtl8169.io_base + reg), value);
        return;
    }
    if (g_rtl8169.access_mode == RTL8169_ACCESS_MMIO && g_rtl8169.mmio_base) {
        volatile uint16_t* ptr = (volatile uint16_t*)(uintptr_t)(g_rtl8169.mmio_base + reg);
        *ptr = value;
        (void)rtl_read16(reg);
    }
}

static void rtl_write32(uint16_t reg, uint32_t value) {
    if (g_rtl8169.access_mode == RTL8169_ACCESS_IO) {
        io_out32((uint16_t)(g_rtl8169.io_base + reg), value);
        return;
    }
    if (g_rtl8169.access_mode == RTL8169_ACCESS_MMIO && g_rtl8169.mmio_base) {
        volatile uint32_t* ptr = (volatile uint32_t*)(uintptr_t)(g_rtl8169.mmio_base + reg);
        *ptr = value;
        (void)rtl_read32(reg);
    }
}

static int rtl8169_supports_device_internal(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != RTL8169_VENDOR_REALTEK) {
        return 0;
    }

    for (uint32_t i = 0; i < sizeof(g_supported_devices) / sizeof(g_supported_devices[0]); i++) {
        if (g_supported_devices[i] == device_id) {
            return 1;
        }
    }
    return 0;
}

static int find_supported_device(uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func, uint16_t* out_dev_id) {
    pci_device_info_t entries[64];
    uint32_t count = 0;

    if (pci_scan_devices(entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])), &count) < 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!rtl8169_supports_device_internal(entries[i].vendor_id, entries[i].device_id)) {
            continue;
        }

        if (out_bus) {
            *out_bus = entries[i].bus;
        }
        if (out_slot) {
            *out_slot = entries[i].slot;
        }
        if (out_func) {
            *out_func = entries[i].func;
        }
        if (out_dev_id) {
            *out_dev_id = entries[i].device_id;
        }
        return 0;
    }

    return -1;
}

static int rtl8169_device_sync(device_t* dev) {
    (void)dev;
    return 0;
}

static const device_ops_t g_rtl8169_device_ops = {
    .api_version = DEVICE_OPS_API_VERSION,
    .sync = rtl8169_device_sync,
};

static void rtl8169_init_rings(void) {
    for (uint32_t i = 0; i < RTL8169_TX_DESC_COUNT; i++) {
        g_tx_desc[i].opts1 = (i == (RTL8169_TX_DESC_COUNT - 1u)) ? RTL8169_DESC_EOR : 0u;
        g_tx_desc[i].opts2 = 0u;
        g_tx_desc[i].addr = (uint64_t)(uintptr_t)&g_tx_buf[i][0];
    }

    for (uint32_t i = 0; i < RTL8169_RX_DESC_COUNT; i++) {
        uint32_t flags = RTL8169_DESC_OWN | RTL8169_RX_BUF_SIZE;
        if (i == (RTL8169_RX_DESC_COUNT - 1u)) {
            flags |= RTL8169_DESC_EOR;
        }
        g_rx_desc[i].opts1 = flags;
        g_rx_desc[i].opts2 = 0u;
        g_rx_desc[i].addr = (uint64_t)(uintptr_t)&g_rx_buf[i][0];
    }

    g_rtl8169.tx_next = 0u;
    g_rtl8169.rx_next = 0u;
}

int rtl8169_supports_device(uint16_t vendor_id, uint16_t device_id) {
    return rtl8169_supports_device_internal(vendor_id, device_id);
}

uint16_t rtl8169_device_id(void) {
    return g_rtl8169.device_id;
}

static int rtl8169_configure_access(uint8_t bus, uint8_t slot, uint8_t func) {
    pci_device_info_t info;
    uint64_t mmio_base = 0u;
    uint16_t io_base = 0u;

    if (pci_get_device_info(bus, slot, func, &info) == 0) {
        for (uint32_t i = 0; i < info.bar_count; i++) {
            if (!info.bars[i].present || info.bars[i].base == 0u) {
                continue;
            }
            if (info.bars[i].is_io) {
                if (io_base == 0u && info.bars[i].base <= 0xFFFFu) {
                    io_base = (uint16_t)(info.bars[i].base & 0xFFFCu);
                }
            } else if (mmio_base == 0u) {
                mmio_base = info.bars[i].base;
            }
        }
    }

    if (mmio_base != 0u) {
        g_rtl8169.access_mode = RTL8169_ACCESS_MMIO;
        g_rtl8169.mmio_base = (volatile uint8_t*)(uintptr_t)mmio_base;
        g_rtl8169.io_base = 0u;
        return 0;
    }
    if (io_base != 0u) {
        g_rtl8169.access_mode = RTL8169_ACCESS_IO;
        g_rtl8169.io_base = io_base;
        g_rtl8169.mmio_base = NULL;
        return 0;
    }

    {
        uint32_t bar0 = pci_config_read32(bus, slot, func, 0x10u);
        if ((bar0 & 0x1u) != 0u) {
            io_base = (uint16_t)(bar0 & 0xFFFCu);
            if (io_base != 0u) {
                g_rtl8169.access_mode = RTL8169_ACCESS_IO;
                g_rtl8169.io_base = io_base;
                g_rtl8169.mmio_base = NULL;
                return 0;
            }
        } else if ((bar0 & 0xFFFFFFF0u) != 0u) {
            g_rtl8169.access_mode = RTL8169_ACCESS_MMIO;
            g_rtl8169.mmio_base = (volatile uint8_t*)(uintptr_t)(uint64_t)(bar0 & 0xFFFFFFF0u);
            g_rtl8169.io_base = 0u;
            return 0;
        }
    }

    g_rtl8169.access_mode = RTL8169_ACCESS_NONE;
    g_rtl8169.io_base = 0u;
    g_rtl8169.mmio_base = NULL;
    return -1;
}

int rtl8169_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id) {
    uint16_t cmd;

    if (!rtl8169_supports_device_internal(RTL8169_VENDOR_REALTEK, dev_id)) {
        return -1;
    }
    if (g_rtl8169.ready) {
        return 0;
    }

    mem_zero(&g_rtl8169, sizeof(g_rtl8169));
    mem_zero(g_tx_desc, sizeof(g_tx_desc));
    mem_zero(g_rx_desc, sizeof(g_rx_desc));
    mem_zero(g_tx_buf, sizeof(g_tx_buf));
    mem_zero(g_rx_buf, sizeof(g_rx_buf));

    g_rtl8169.pci_bus = bus;
    g_rtl8169.pci_slot = slot;
    g_rtl8169.pci_func = func;
    g_rtl8169.device_id = dev_id;

    cmd = pci_config_read16(bus, slot, func, 0x04u);
    cmd = (uint16_t)(cmd | 0x0006u); /* memory + bus mastering */
    cmd = (uint16_t)(cmd | 0x0001u); /* I/O too (harmless for MMIO mode) */
    pci_config_write16(bus, slot, func, 0x04u, cmd);

    if (rtl8169_configure_access(bus, slot, func) != 0) {
        return -1;
    }

    rtl_write8(RTL8169_REG_9346CR, 0xC0u);
    rtl_write8(RTL8169_REG_CR, RTL8169_CR_RST);
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((rtl_read8(RTL8169_REG_CR) & RTL8169_CR_RST) == 0u) {
            break;
        }
        cpu_relax();
    }

    rtl8169_init_rings();

    rtl_write16(RTL8169_REG_IMR, 0u);
    rtl_write16(RTL8169_REG_ISR, 0xFFFFu);
    rtl_write16(RTL8169_REG_RMS, 1536u);
    rtl_write16(RTL8169_REG_CPLUSCMD, 0x0000u);
    rtl_write32(RTL8169_REG_TNPDS_LO, (uint32_t)(uintptr_t)&g_tx_desc[0]);
    rtl_write32(RTL8169_REG_TNPDS_HI, (uint32_t)((uint64_t)(uintptr_t)&g_tx_desc[0] >> 32));
    rtl_write32(RTL8169_REG_RDSAR_LO, (uint32_t)(uintptr_t)&g_rx_desc[0]);
    rtl_write32(RTL8169_REG_RDSAR_HI, (uint32_t)((uint64_t)(uintptr_t)&g_rx_desc[0] >> 32));
    rtl_write32(RTL8169_REG_TCR, 0x03000700u);
    rtl_write32(
        RTL8169_REG_RCR,
        RTL8169_RX_ACCEPT_BCAST | RTL8169_RX_ACCEPT_MCAST | RTL8169_RX_ACCEPT_MY_PHYS | RTL8169_RX_ACCEPT_ALL_PHYS
    );

    rtl_write8(RTL8169_REG_CR, RTL8169_CR_RE | RTL8169_CR_TE);
    rtl_write8(RTL8169_REG_9346CR, 0x00u);

    for (uint32_t i = 0; i < 6u; i++) {
        g_rtl8169.mac[i] = rtl_read8((uint16_t)(RTL8169_REG_IDR0 + i));
    }

    g_rtl8169.ready = 1u;
    (void)device_register(MYAOS_DEV_NETWORK, "eth0", "net.rtl8169", NULL, &g_rtl8169_device_ops, NULL);
    return 0;
}

int rtl8169_init(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t dev_id = 0;

    if (find_supported_device(&bus, &slot, &func, &dev_id) != 0) {
        return -1;
    }
    return rtl8169_init_pci(bus, slot, func, dev_id);
}

int rtl8169_ready(void) {
    return g_rtl8169.ready ? 1 : 0;
}

int rtl8169_get_mac(uint8_t out_mac[6]) {
    if (!out_mac || !g_rtl8169.ready) {
        return -1;
    }
    for (uint32_t i = 0; i < 6u; i++) {
        out_mac[i] = g_rtl8169.mac[i];
    }
    return 0;
}

int rtl8169_send_frame(const void* data, uint16_t len) {
    uint16_t idx;
    uint32_t base_flags;
    uint32_t opts1;

    if (!g_rtl8169.ready || !data || len == 0u || len > 1514u) {
        return -1;
    }

    idx = g_rtl8169.tx_next;
    if ((g_tx_desc[idx].opts1 & RTL8169_DESC_OWN) != 0u) {
        return -1;
    }

    mem_copy(&g_tx_buf[idx][0], data, len);
    base_flags = (idx == (RTL8169_TX_DESC_COUNT - 1u)) ? RTL8169_DESC_EOR : 0u;
    opts1 = base_flags | RTL8169_DESC_FS | RTL8169_DESC_LS | (uint32_t)len | RTL8169_DESC_OWN;
    g_tx_desc[idx].opts2 = 0u;
    barrier();
    g_tx_desc[idx].opts1 = opts1;
    barrier();
    rtl_write8(RTL8169_REG_TPPOLL, RTL8169_TPPOLL_NPQ);

    for (uint32_t i = 0; i < RTL8169_TX_TIMEOUT_LOOPS; i++) {
        barrier();
        if ((g_tx_desc[idx].opts1 & RTL8169_DESC_OWN) == 0u) {
            g_rtl8169.tx_next = (uint16_t)((idx + 1u) % RTL8169_TX_DESC_COUNT);
            return 0;
        }
        cpu_relax();
    }

    return -1;
}

int rtl8169_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len) {
    uint16_t idx;
    uint32_t opts1;
    uint16_t len;
    uint16_t copy_len;
    uint32_t flags;

    if (!g_rtl8169.ready || !out_len || (max_len != 0u && !out_buf)) {
        return -1;
    }

    idx = g_rtl8169.rx_next;
    barrier();
    opts1 = g_rx_desc[idx].opts1;
    if ((opts1 & RTL8169_DESC_OWN) != 0u) {
        return -1;
    }

    flags = opts1 & (RTL8169_DESC_FS | RTL8169_DESC_LS);
    len = (uint16_t)(opts1 & RTL8169_DESC_LEN_MASK);
    if (flags != (RTL8169_DESC_FS | RTL8169_DESC_LS) || len <= 4u) {
        *out_len = 0u;
    } else {
        len = (uint16_t)(len - 4u);
        copy_len = len;
        if (copy_len > max_len) {
            copy_len = max_len;
        }
        if (copy_len > 0u) {
            mem_copy(out_buf, &g_rx_buf[idx][0], copy_len);
        }
        *out_len = copy_len;
    }

    flags = RTL8169_DESC_OWN | RTL8169_RX_BUF_SIZE;
    if (idx == (RTL8169_RX_DESC_COUNT - 1u)) {
        flags |= RTL8169_DESC_EOR;
    }
    g_rx_desc[idx].opts2 = 0u;
    barrier();
    g_rx_desc[idx].opts1 = flags;
    g_rtl8169.rx_next = (uint16_t)((idx + 1u) % RTL8169_RX_DESC_COUNT);
    rtl_write16(RTL8169_REG_ISR, 0xFFFFu);
    return (*out_len > 0u) ? 0 : -1;
}
