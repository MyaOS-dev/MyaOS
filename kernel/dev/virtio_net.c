#include "virtio_net.h"
#include "device.h"
#include "pci.h"
#include <stddef.h>

#define VIRTIO_VENDOR 0x1AF4u

#define VIRTIO_NET_DEV_LEGACY 0x1000u

#define VIRTIO_PCI_REG_HOST_FEATURES 0x00u
#define VIRTIO_PCI_REG_GUEST_FEATURES 0x04u
#define VIRTIO_PCI_REG_QUEUE_PFN 0x08u
#define VIRTIO_PCI_REG_QUEUE_NUM 0x0Cu
#define VIRTIO_PCI_REG_QUEUE_SEL 0x0Eu
#define VIRTIO_PCI_REG_QUEUE_NOTIFY 0x10u
#define VIRTIO_PCI_REG_STATUS 0x12u
#define VIRTIO_PCI_REG_ISR 0x13u
#define VIRTIO_PCI_REG_MAC 0x14u

#define VIRTIO_STATUS_ACK 0x01u
#define VIRTIO_STATUS_DRIVER 0x02u
#define VIRTIO_STATUS_DRIVER_OK 0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_STATUS_FAILED 0x80u

#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u

#define VIRTQ_ALIGN 4096u
#define VIRTQ_MAX_SIZE 256u
#define VIRTQ_MEM_BYTES 12288u

#define VIRTIO_NET_Q_RX 0u
#define VIRTIO_NET_Q_TX 1u

#define VIRTIO_NET_HDR_LEN 10u
#define VIRTIO_NET_FRAME_MAX 1518u
#define VIRTIO_NET_RX_DESC_COUNT 8u
#define VIRTIO_NET_TX_DESC_COUNT 8u
#define VIRTIO_NET_TX_TIMEOUT_LOOPS 300000u

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTQ_MAX_SIZE];
    uint16_t used_event;
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTQ_MAX_SIZE];
    uint16_t avail_event;
} virtq_used_t;

typedef struct {
    uint16_t size;
    volatile virtq_desc_t* desc;
    volatile virtq_avail_t* avail;
    volatile virtq_used_t* used;
    uint16_t last_used_idx;
} virtq_state_t;

typedef struct {
    uint8_t ready;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint16_t io_base;
    uint16_t device_id;
    uint8_t mac[6];
    uint8_t reserved0[2];
    virtq_state_t rxq;
    virtq_state_t txq;
    uint16_t rx_desc_count;
    uint16_t tx_desc_count;
    uint16_t tx_last_sent_desc;
    uint8_t tx_inflight;
    uint8_t reserved1;
} virtio_net_state_t;

static virtio_net_state_t g_vnet;
static uint8_t g_rx_ring_mem[VIRTQ_MEM_BYTES] __attribute__((aligned(VIRTQ_ALIGN)));
static uint8_t g_tx_ring_mem[VIRTQ_MEM_BYTES] __attribute__((aligned(VIRTQ_ALIGN)));
static uint8_t g_rx_buf[VIRTIO_NET_RX_DESC_COUNT][VIRTIO_NET_HDR_LEN + VIRTIO_NET_FRAME_MAX] __attribute__((aligned(16)));
static uint8_t g_tx_buf[VIRTIO_NET_TX_DESC_COUNT][VIRTIO_NET_HDR_LEN + VIRTIO_NET_FRAME_MAX] __attribute__((aligned(16)));

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

static uintptr_t align_up_uintptr(uintptr_t value, uintptr_t align) {
    return (value + align - 1u) & ~(align - 1u);
}

static int virtio_net_supports_device_internal(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != VIRTIO_VENDOR) {
        return 0;
    }
    return (device_id == VIRTIO_NET_DEV_LEGACY) ? 1 : 0;
}

static int find_supported_device(uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func, uint16_t* out_dev_id) {
    pci_device_info_t entries[64];
    uint32_t count = 0;

    if (pci_scan_devices(entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])), &count) < 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!virtio_net_supports_device_internal(entries[i].vendor_id, entries[i].device_id)) {
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

static int virtio_device_sync(device_t* dev) {
    (void)dev;
    return 0;
}

static const device_ops_t g_vnet_device_ops = {
    .api_version = DEVICE_OPS_API_VERSION,
    .sync = virtio_device_sync,
};

static int virtq_setup(uint16_t io_base, uint16_t queue_sel, uint8_t* mem, size_t mem_size, virtq_state_t* out_q) {
    uint16_t qsize;
    uintptr_t mem_base;
    uintptr_t avail_addr;
    uintptr_t used_addr;
    size_t avail_bytes;
    size_t needed;

    if (!mem || !out_q) {
        return -1;
    }

    io_out16((uint16_t)(io_base + VIRTIO_PCI_REG_QUEUE_SEL), queue_sel);
    qsize = io_in16((uint16_t)(io_base + VIRTIO_PCI_REG_QUEUE_NUM));
    if (qsize == 0u || qsize > VIRTQ_MAX_SIZE) {
        return -1;
    }

    mem_zero(mem, mem_size);
    mem_base = (uintptr_t)mem;

    out_q->size = qsize;
    out_q->desc = (volatile virtq_desc_t*)mem_base;
    avail_addr = mem_base + (uintptr_t)(sizeof(virtq_desc_t) * qsize);
    out_q->avail = (volatile virtq_avail_t*)avail_addr;

    avail_bytes = (size_t)(sizeof(uint16_t) * 2u + sizeof(uint16_t) * qsize + sizeof(uint16_t));
    used_addr = align_up_uintptr(avail_addr + (uintptr_t)avail_bytes, VIRTQ_ALIGN);
    out_q->used = (volatile virtq_used_t*)used_addr;

    needed = (size_t)((used_addr + (uintptr_t)(sizeof(uint16_t) * 2u + sizeof(virtq_used_elem_t) * qsize + sizeof(uint16_t))) -
                      mem_base);
    if (needed > mem_size) {
        return -1;
    }

    out_q->last_used_idx = 0u;
    io_out32((uint16_t)(io_base + VIRTIO_PCI_REG_QUEUE_PFN), (uint32_t)(mem_base >> 12));
    return 0;
}

static void virtq_notify(uint16_t io_base, uint16_t queue_sel) {
    io_out16((uint16_t)(io_base + VIRTIO_PCI_REG_QUEUE_NOTIFY), queue_sel);
}

static int virtio_tx_wait_complete(uint32_t timeout_loops) {
    uint16_t used_idx;

    if (!g_vnet.tx_inflight) {
        return 0;
    }

    for (uint32_t i = 0; i < timeout_loops; i++) {
        barrier();
        used_idx = g_vnet.txq.used->idx;
        if (used_idx != g_vnet.txq.last_used_idx) {
            g_vnet.txq.last_used_idx = used_idx;
            g_vnet.tx_inflight = 0u;
            (void)io_in8((uint16_t)(g_vnet.io_base + VIRTIO_PCI_REG_ISR));
            return 0;
        }
        cpu_relax();
    }

    return -1;
}

int virtio_net_supports_device(uint16_t vendor_id, uint16_t device_id) {
    return virtio_net_supports_device_internal(vendor_id, device_id);
}

uint16_t virtio_net_device_id(void) {
    return g_vnet.device_id;
}

int virtio_net_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id) {
    uint16_t cmd;
    uint32_t bar0;
    uint16_t io_base;
    uint16_t rx_desc_target;
    uint16_t tx_desc_target;

    if (!virtio_net_supports_device_internal(VIRTIO_VENDOR, dev_id)) {
        return -1;
    }
    if (g_vnet.ready) {
        return 0;
    }

    mem_zero(&g_vnet, sizeof(g_vnet));
    mem_zero(g_rx_ring_mem, sizeof(g_rx_ring_mem));
    mem_zero(g_tx_ring_mem, sizeof(g_tx_ring_mem));
    mem_zero(g_rx_buf, sizeof(g_rx_buf));
    mem_zero(g_tx_buf, sizeof(g_tx_buf));

    g_vnet.pci_bus = bus;
    g_vnet.pci_slot = slot;
    g_vnet.pci_func = func;
    g_vnet.device_id = dev_id;

    cmd = pci_config_read16(bus, slot, func, 0x04u);
    cmd = (uint16_t)(cmd | 0x0005u); /* I/O + bus mastering */
    pci_config_write16(bus, slot, func, 0x04u, cmd);

    bar0 = pci_config_read32(bus, slot, func, 0x10u);
    if ((bar0 & 0x1u) == 0u) {
        return -1;
    }

    io_base = (uint16_t)(bar0 & 0xFFFCu);
    if (io_base == 0u) {
        return -1;
    }
    g_vnet.io_base = io_base;

    io_out8((uint16_t)(io_base + VIRTIO_PCI_REG_STATUS), 0u);
    io_out8((uint16_t)(io_base + VIRTIO_PCI_REG_STATUS), VIRTIO_STATUS_ACK);
    io_out8((uint16_t)(io_base + VIRTIO_PCI_REG_STATUS), (uint8_t)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER));

    (void)io_in32((uint16_t)(io_base + VIRTIO_PCI_REG_HOST_FEATURES));
    io_out32((uint16_t)(io_base + VIRTIO_PCI_REG_GUEST_FEATURES), 0u);
    io_out8(
        (uint16_t)(io_base + VIRTIO_PCI_REG_STATUS),
        (uint8_t)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK)
    );

    if (virtq_setup(io_base, VIRTIO_NET_Q_RX, g_rx_ring_mem, sizeof(g_rx_ring_mem), &g_vnet.rxq) != 0 ||
        virtq_setup(io_base, VIRTIO_NET_Q_TX, g_tx_ring_mem, sizeof(g_tx_ring_mem), &g_vnet.txq) != 0) {
        io_out8(
            (uint16_t)(io_base + VIRTIO_PCI_REG_STATUS),
            (uint8_t)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FAILED)
        );
        return -1;
    }

    rx_desc_target = VIRTIO_NET_RX_DESC_COUNT;
    if (rx_desc_target > g_vnet.rxq.size) {
        rx_desc_target = g_vnet.rxq.size;
    }
    tx_desc_target = VIRTIO_NET_TX_DESC_COUNT;
    if (tx_desc_target > g_vnet.txq.size) {
        tx_desc_target = g_vnet.txq.size;
    }
    if (rx_desc_target == 0u || tx_desc_target == 0u) {
        io_out8(
            (uint16_t)(io_base + VIRTIO_PCI_REG_STATUS),
            (uint8_t)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FAILED)
        );
        return -1;
    }

    g_vnet.rx_desc_count = rx_desc_target;
    g_vnet.tx_desc_count = tx_desc_target;
    g_vnet.tx_last_sent_desc = 0u;
    g_vnet.tx_inflight = 0u;

    for (uint16_t i = 0; i < g_vnet.rx_desc_count; i++) {
        g_vnet.rxq.desc[i].addr = (uint64_t)(uintptr_t)&g_rx_buf[i][0];
        g_vnet.rxq.desc[i].len = (uint32_t)(VIRTIO_NET_HDR_LEN + VIRTIO_NET_FRAME_MAX);
        g_vnet.rxq.desc[i].flags = VIRTQ_DESC_F_WRITE;
        g_vnet.rxq.desc[i].next = 0u;
        g_vnet.rxq.avail->ring[g_vnet.rxq.avail->idx % g_vnet.rxq.size] = i;
        g_vnet.rxq.avail->idx++;
    }
    barrier();
    virtq_notify(io_base, VIRTIO_NET_Q_RX);

    for (uint16_t i = 0; i < g_vnet.tx_desc_count; i++) {
        g_vnet.txq.desc[i].addr = (uint64_t)(uintptr_t)&g_tx_buf[i][0];
        g_vnet.txq.desc[i].len = 0u;
        g_vnet.txq.desc[i].flags = 0u;
        g_vnet.txq.desc[i].next = 0u;
    }

    for (uint32_t i = 0; i < 6u; i++) {
        g_vnet.mac[i] = io_in8((uint16_t)(io_base + VIRTIO_PCI_REG_MAC + i));
    }

    io_out8(
        (uint16_t)(io_base + VIRTIO_PCI_REG_STATUS),
        (uint8_t)(VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK)
    );

    g_vnet.ready = 1u;
    (void)device_register(MYAOS_DEV_NETWORK, "eth0", "net.virtio", NULL, &g_vnet_device_ops, NULL);
    return 0;
}

int virtio_net_init(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t dev_id = 0;

    if (find_supported_device(&bus, &slot, &func, &dev_id) != 0) {
        return -1;
    }
    return virtio_net_init_pci(bus, slot, func, dev_id);
}

int virtio_net_ready(void) {
    return g_vnet.ready ? 1 : 0;
}

int virtio_net_get_mac(uint8_t out_mac[6]) {
    if (!out_mac || !g_vnet.ready) {
        return -1;
    }

    for (uint32_t i = 0; i < 6u; i++) {
        out_mac[i] = g_vnet.mac[i];
    }
    return 0;
}

int virtio_net_send_frame(const void* data, uint16_t len) {
    uint16_t desc_id;
    uint16_t packet_len;

    if (!g_vnet.ready || !data || len == 0u || len > 1514u) {
        return -1;
    }

    if (virtio_tx_wait_complete(VIRTIO_NET_TX_TIMEOUT_LOOPS) != 0) {
        return -1;
    }

    desc_id = g_vnet.tx_last_sent_desc;
    g_vnet.tx_last_sent_desc = (uint16_t)((g_vnet.tx_last_sent_desc + 1u) % g_vnet.tx_desc_count);

    mem_zero(&g_tx_buf[desc_id][0], VIRTIO_NET_HDR_LEN);
    mem_copy(&g_tx_buf[desc_id][VIRTIO_NET_HDR_LEN], data, len);
    packet_len = (uint16_t)(VIRTIO_NET_HDR_LEN + len);

    g_vnet.txq.desc[desc_id].addr = (uint64_t)(uintptr_t)&g_tx_buf[desc_id][0];
    g_vnet.txq.desc[desc_id].len = packet_len;
    g_vnet.txq.desc[desc_id].flags = 0u;
    g_vnet.txq.desc[desc_id].next = 0u;

    g_vnet.txq.avail->ring[g_vnet.txq.avail->idx % g_vnet.txq.size] = desc_id;
    barrier();
    g_vnet.txq.avail->idx++;
    barrier();
    virtq_notify(g_vnet.io_base, VIRTIO_NET_Q_TX);
    g_vnet.tx_inflight = 1u;

    return virtio_tx_wait_complete(VIRTIO_NET_TX_TIMEOUT_LOOPS);
}

int virtio_net_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len) {
    uint16_t used_idx;
    virtq_used_elem_t used_elem;
    uint16_t desc_id;
    uint16_t payload_len;
    uint16_t copy_len;

    if (!g_vnet.ready || !out_len || (max_len != 0u && !out_buf)) {
        return -1;
    }

    barrier();
    used_idx = g_vnet.rxq.used->idx;
    if (used_idx == g_vnet.rxq.last_used_idx) {
        return -1;
    }

    used_elem = g_vnet.rxq.used->ring[g_vnet.rxq.last_used_idx % g_vnet.rxq.size];
    g_vnet.rxq.last_used_idx++;

    desc_id = (uint16_t)(used_elem.id & 0xFFFFu);
    if (desc_id >= g_vnet.rx_desc_count) {
        return -1;
    }

    if (used_elem.len <= VIRTIO_NET_HDR_LEN) {
        payload_len = 0u;
    } else {
        uint32_t raw_payload = used_elem.len - VIRTIO_NET_HDR_LEN;
        payload_len = (raw_payload > 0xFFFFu) ? 0xFFFFu : (uint16_t)raw_payload;
    }

    copy_len = payload_len;
    if (copy_len > max_len) {
        copy_len = max_len;
    }
    if (copy_len > 0u) {
        mem_copy(out_buf, &g_rx_buf[desc_id][VIRTIO_NET_HDR_LEN], copy_len);
    }
    *out_len = copy_len;

    g_vnet.rxq.desc[desc_id].addr = (uint64_t)(uintptr_t)&g_rx_buf[desc_id][0];
    g_vnet.rxq.desc[desc_id].len = (uint32_t)(VIRTIO_NET_HDR_LEN + VIRTIO_NET_FRAME_MAX);
    g_vnet.rxq.desc[desc_id].flags = VIRTQ_DESC_F_WRITE;
    g_vnet.rxq.desc[desc_id].next = 0u;
    g_vnet.rxq.avail->ring[g_vnet.rxq.avail->idx % g_vnet.rxq.size] = desc_id;
    barrier();
    g_vnet.rxq.avail->idx++;
    barrier();
    virtq_notify(g_vnet.io_base, VIRTIO_NET_Q_RX);
    (void)io_in8((uint16_t)(g_vnet.io_base + VIRTIO_PCI_REG_ISR));
    return (copy_len > 0u) ? 0 : -1;
}
