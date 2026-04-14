#include "rtl8139.h"
#include "device.h"
#include "pci.h"
#include <stddef.h>

#define RTL8139_VENDOR_REALTEK 0x10ECu

#define RTL8139_REG_IDR0 0x00u
#define RTL8139_REG_TSD0 0x10u
#define RTL8139_REG_TSAD0 0x20u
#define RTL8139_REG_RBSTART 0x30u
#define RTL8139_REG_CR 0x37u
#define RTL8139_REG_CAPR 0x38u
#define RTL8139_REG_IMR 0x3Cu
#define RTL8139_REG_ISR 0x3Eu
#define RTL8139_REG_TCR 0x40u
#define RTL8139_REG_RCR 0x44u
#define RTL8139_REG_CONFIG1 0x52u

#define RTL8139_CR_RE 0x08u
#define RTL8139_CR_TE 0x04u
#define RTL8139_CR_RST 0x10u
#define RTL8139_CR_BUFE 0x01u

#define RTL8139_ISR_ROK 0x0001u

#define RTL8139_RCR_AAP 0x00000001u
#define RTL8139_RCR_APM 0x00000002u
#define RTL8139_RCR_AM 0x00000004u
#define RTL8139_RCR_AB 0x00000008u
#define RTL8139_RCR_WRAP 0x00000080u

#define RTL8139_TSD_TOK 0x00008000u
#define RTL8139_TSD_TER 0x00004000u

#define RTL8139_TX_DESC_COUNT 4u
#define RTL8139_TX_BUF_SIZE 2048u
#define RTL8139_RX_RING_SIZE 8192u
#define RTL8139_RX_STATUS_ROK 0x0001u

typedef struct {
    uint8_t ready;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint16_t io_base;
    uint16_t device_id;
    uint8_t tx_next;
    uint8_t reserved0;
    uint32_t rx_offset;
    uint8_t mac[6];
    uint8_t reserved1[2];
} rtl8139_state_t;

static rtl8139_state_t g_rtl8139;
static uint8_t g_tx_buf[RTL8139_TX_DESC_COUNT][RTL8139_TX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t g_rx_ring[RTL8139_RX_RING_SIZE] __attribute__((aligned(16)));

static const uint16_t g_supported_devices[] = {
    0x8139u, /* RTL8139 */
    0x8138u, /* RTL8139C+/8169-compatible revisions still expose this ID */
};

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

static inline uint32_t io_in32(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t ring_read8(uint32_t off) {
    return g_rx_ring[off % RTL8139_RX_RING_SIZE];
}

static uint16_t ring_read16(uint32_t off) {
    uint16_t lo = ring_read8(off);
    uint16_t hi = ring_read8(off + 1u);
    return (uint16_t)(lo | (uint16_t)(hi << 8));
}

static void ring_copy(uint32_t off, uint8_t* out, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        out[i] = ring_read8(off + i);
    }
}

static int rtl8139_supports_device_internal(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != RTL8139_VENDOR_REALTEK) {
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
        if (!rtl8139_supports_device_internal(entries[i].vendor_id, entries[i].device_id)) {
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

static int rtl8139_device_sync(device_t* dev) {
    (void)dev;
    return 0;
}

static const device_ops_t g_rtl8139_device_ops = {
    .api_version = DEVICE_OPS_API_VERSION,
    .sync = rtl8139_device_sync,
};

int rtl8139_supports_device(uint16_t vendor_id, uint16_t device_id) {
    return rtl8139_supports_device_internal(vendor_id, device_id);
}

uint16_t rtl8139_device_id(void) {
    return g_rtl8139.device_id;
}

int rtl8139_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id) {
    uint16_t cmd;
    uint32_t bar0;
    uint16_t io_base;

    if (!rtl8139_supports_device_internal(RTL8139_VENDOR_REALTEK, dev_id)) {
        return -1;
    }
    if (g_rtl8139.ready) {
        return 0;
    }

    mem_zero(&g_rtl8139, sizeof(g_rtl8139));
    mem_zero(g_tx_buf, sizeof(g_tx_buf));
    mem_zero(g_rx_ring, sizeof(g_rx_ring));

    g_rtl8139.pci_bus = bus;
    g_rtl8139.pci_slot = slot;
    g_rtl8139.pci_func = func;
    g_rtl8139.device_id = dev_id;

    cmd = pci_config_read16(bus, slot, func, 0x04u);
    cmd = (uint16_t)(cmd | 0x0005u); /* I/O space + bus mastering */
    pci_config_write16(bus, slot, func, 0x04u, cmd);

    bar0 = pci_config_read32(bus, slot, func, 0x10u);
    if ((bar0 & 0x1u) == 0u) {
        return -1;
    }

    io_base = (uint16_t)(bar0 & 0xFFFCu);
    if (io_base == 0u) {
        return -1;
    }
    g_rtl8139.io_base = io_base;

    io_out8((uint16_t)(io_base + RTL8139_REG_CONFIG1), 0x00u);

    io_out8((uint16_t)(io_base + RTL8139_REG_CR), RTL8139_CR_RST);
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((io_in8((uint16_t)(io_base + RTL8139_REG_CR)) & RTL8139_CR_RST) == 0u) {
            break;
        }
        cpu_relax();
    }

    io_out32((uint16_t)(io_base + RTL8139_REG_RBSTART), (uint32_t)(uintptr_t)&g_rx_ring[0]);
    for (uint32_t i = 0; i < RTL8139_TX_DESC_COUNT; i++) {
        io_out32(
            (uint16_t)(io_base + RTL8139_REG_TSAD0 + (uint16_t)(i * 4u)),
            (uint32_t)(uintptr_t)&g_tx_buf[i][0]
        );
    }

    io_out16((uint16_t)(io_base + RTL8139_REG_IMR), 0u);
    io_out16((uint16_t)(io_base + RTL8139_REG_ISR), 0xFFFFu);
    io_out32((uint16_t)(io_base + RTL8139_REG_TCR), 0x03000700u);
    io_out32(
        (uint16_t)(io_base + RTL8139_REG_RCR),
        RTL8139_RCR_APM | RTL8139_RCR_AM | RTL8139_RCR_AB | RTL8139_RCR_AAP | RTL8139_RCR_WRAP
    );

    g_rtl8139.rx_offset = 0u;
    io_out16((uint16_t)(io_base + RTL8139_REG_CAPR), 0u);
    io_out8((uint16_t)(io_base + RTL8139_REG_CR), RTL8139_CR_RE | RTL8139_CR_TE);

    for (uint32_t i = 0; i < 6u; i++) {
        g_rtl8139.mac[i] = io_in8((uint16_t)(io_base + RTL8139_REG_IDR0 + i));
    }

    g_rtl8139.ready = 1u;
    (void)device_register(MYAOS_DEV_NETWORK, "eth0", "net.rtl8139", NULL, &g_rtl8139_device_ops, NULL);
    return 0;
}

int rtl8139_init(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t dev_id = 0;

    if (find_supported_device(&bus, &slot, &func, &dev_id) != 0) {
        return -1;
    }
    return rtl8139_init_pci(bus, slot, func, dev_id);
}

int rtl8139_ready(void) {
    return g_rtl8139.ready ? 1 : 0;
}

int rtl8139_get_mac(uint8_t out_mac[6]) {
    if (!out_mac || !g_rtl8139.ready) {
        return -1;
    }

    for (uint32_t i = 0; i < 6u; i++) {
        out_mac[i] = g_rtl8139.mac[i];
    }
    return 0;
}

int rtl8139_send_frame(const void* data, uint16_t len) {
    uint32_t idx;
    uint16_t io_base;
    uint32_t tsd;

    if (!g_rtl8139.ready || !data || len == 0u || len > 1514u) {
        return -1;
    }

    idx = g_rtl8139.tx_next % RTL8139_TX_DESC_COUNT;
    io_base = g_rtl8139.io_base;
    mem_copy(&g_tx_buf[idx][0], data, len);

    io_out32((uint16_t)(io_base + RTL8139_REG_TSAD0 + (uint16_t)(idx * 4u)), (uint32_t)(uintptr_t)&g_tx_buf[idx][0]);
    io_out32((uint16_t)(io_base + RTL8139_REG_TSD0 + (uint16_t)(idx * 4u)), len);

    for (uint32_t spin = 0; spin < 200000u; spin++) {
        tsd = io_in32((uint16_t)(io_base + RTL8139_REG_TSD0 + (uint16_t)(idx * 4u)));
        if ((tsd & RTL8139_TSD_TOK) != 0u) {
            g_rtl8139.tx_next = (uint8_t)((idx + 1u) % RTL8139_TX_DESC_COUNT);
            return 0;
        }
        if ((tsd & RTL8139_TSD_TER) != 0u) {
            return -1;
        }
        cpu_relax();
    }

    return -1;
}

int rtl8139_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len) {
    uint16_t io_base;
    uint32_t off;
    uint16_t pkt_status;
    uint16_t pkt_len_raw;
    uint16_t payload_len;
    uint16_t copy_len;
    uint32_t next_off;
    uint16_t capr;

    if (!g_rtl8139.ready || !out_len || (max_len != 0u && !out_buf)) {
        return -1;
    }

    io_base = g_rtl8139.io_base;
    if ((io_in8((uint16_t)(io_base + RTL8139_REG_CR)) & RTL8139_CR_BUFE) != 0u) {
        return -1;
    }

    off = g_rtl8139.rx_offset % RTL8139_RX_RING_SIZE;
    pkt_status = ring_read16(off);
    pkt_len_raw = ring_read16(off + 2u);

    if ((pkt_status & RTL8139_RX_STATUS_ROK) == 0u || pkt_len_raw < 4u || pkt_len_raw > 1792u) {
        io_out16((uint16_t)(io_base + RTL8139_REG_ISR), 0xFFFFu);
        return -1;
    }

    payload_len = (uint16_t)(pkt_len_raw - 4u);
    copy_len = payload_len;
    if (copy_len > max_len) {
        copy_len = max_len;
    }
    if (copy_len > 0u) {
        ring_copy(off + 4u, (uint8_t*)out_buf, copy_len);
    }
    *out_len = copy_len;

    next_off = (off + pkt_len_raw + 4u + 3u) & ~3u;
    g_rtl8139.rx_offset = next_off % RTL8139_RX_RING_SIZE;

    if (g_rtl8139.rx_offset >= 16u) {
        capr = (uint16_t)(g_rtl8139.rx_offset - 16u);
    } else {
        capr = (uint16_t)(RTL8139_RX_RING_SIZE + g_rtl8139.rx_offset - 16u);
    }
    io_out16((uint16_t)(io_base + RTL8139_REG_CAPR), capr);
    io_out16((uint16_t)(io_base + RTL8139_REG_ISR), RTL8139_ISR_ROK);
    return 0;
}
