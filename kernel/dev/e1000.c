#include "e1000.h"
#include "apic.h"
#include "device.h"
#include "pci.h"
#include <stddef.h>

#define E1000_VENDOR_INTEL 0x8086u

#define E1000_REG_CTRL 0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_RCTL 0x0100u
#define E1000_REG_RDBAL 0x2800u
#define E1000_REG_RDBAH 0x2804u
#define E1000_REG_RDLEN 0x2808u
#define E1000_REG_RDH 0x2810u
#define E1000_REG_RDT 0x2818u
#define E1000_REG_TCTL 0x0400u
#define E1000_REG_TIPG 0x0410u
#define E1000_REG_TDBAL 0x3800u
#define E1000_REG_TDBAH 0x3804u
#define E1000_REG_TDLEN 0x3808u
#define E1000_REG_TDH 0x3810u
#define E1000_REG_TDT 0x3818u
#define E1000_REG_IMC 0x00D8u
#define E1000_REG_ICR 0x00C0u
#define E1000_REG_RAL0 0x5400u
#define E1000_REG_RAH0 0x5404u

#define E1000_CTRL_RST 0x04000000u
#define E1000_TCTL_EN 0x00000002u
#define E1000_TCTL_PSP 0x00000008u
#define E1000_TCTL_CT_SHIFT 4u
#define E1000_TCTL_COLD_SHIFT 12u
#define E1000_RCTL_EN 0x00000002u
#define E1000_RCTL_UPE 0x00000008u
#define E1000_RCTL_MPE 0x00000010u
#define E1000_RCTL_BAM 0x00008000u
#define E1000_RCTL_SECRC 0x04000000u

#define E1000_TX_DESC_COUNT 16u
#define E1000_TX_BUF_SIZE 2048u
#define E1000_RX_DESC_COUNT 32u
#define E1000_RX_BUF_SIZE 2048u
#define E1000_TX_CMD_EOP 0x01u
#define E1000_TX_CMD_IFCS 0x02u
#define E1000_TX_CMD_RS 0x08u
#define E1000_TX_STATUS_DD 0x01u
#define E1000_RX_STATUS_DD 0x01u

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct {
    uint8_t ready;
    uint8_t msi_enabled;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint8_t reserved0;
    uint16_t device_id;
    uint8_t reserved1;
    volatile uint8_t* mmio;
    uint8_t mac[6];
    uint8_t reserved2[2];
    uint32_t rx_next;
} e1000_state_t;

static e1000_state_t g_e1000;
static e1000_tx_desc_t g_tx_desc[E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t g_tx_buf[E1000_TX_DESC_COUNT][E1000_TX_BUF_SIZE] __attribute__((aligned(16)));
static e1000_rx_desc_t g_rx_desc[E1000_RX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t g_rx_buf[E1000_RX_DESC_COUNT][E1000_RX_BUF_SIZE] __attribute__((aligned(16)));

static const uint16_t g_supported_devices[] = {
    /* Legacy e1000 in common virtualized setups */
    0x100Eu,
    0x100Fu,
    0x1010u,
    0x1011u,
    0x1026u,
    0x107Cu,
    /* Newer e1000/e1000e family variants */
    0x10D3u,
    0x10EAu,
    0x1502u,
    0x1503u,
    0x153Au,
    /* I210 family */
    0x1533u,
    0x1536u,
    0x1537u,
    0x1538u,
    0x157Bu,
    /* I219 family (common variants) */
    0x15B7u,
    0x15B8u,
    0x15B9u,
    0x15D6u,
    0x15D7u,
    0x15E3u,
    /* I350 family */
    0x1521u,
    0x1522u,
    0x1523u,
    0x1524u,
};

static void mem_zero(void* ptr, size_t size) {
    uint8_t* out = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        out[i] = 0;
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

static uint32_t reg_read(uint32_t reg) {
    volatile uint32_t* ptr;

    if (!g_e1000.mmio) {
        return 0u;
    }

    ptr = (volatile uint32_t*)(uintptr_t)(g_e1000.mmio + reg);
    return *ptr;
}

static void reg_write(uint32_t reg, uint32_t value) {
    volatile uint32_t* ptr;

    if (!g_e1000.mmio) {
        return;
    }

    ptr = (volatile uint32_t*)(uintptr_t)(g_e1000.mmio + reg);
    *ptr = value;
    (void)reg_read(E1000_REG_STATUS);
}

static int e1000_supports_device_internal(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != E1000_VENDOR_INTEL) {
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
        if (!e1000_supports_device_internal(entries[i].vendor_id, entries[i].device_id)) {
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

static void init_tx_ring(void) {
    uint64_t tx_phys = (uint64_t)(uintptr_t)&g_tx_desc[0];

    for (uint32_t i = 0; i < E1000_TX_DESC_COUNT; i++) {
        g_tx_desc[i].addr = (uint64_t)(uintptr_t)&g_tx_buf[i][0];
        g_tx_desc[i].length = 0u;
        g_tx_desc[i].cso = 0u;
        g_tx_desc[i].cmd = 0u;
        g_tx_desc[i].status = E1000_TX_STATUS_DD;
        g_tx_desc[i].css = 0u;
        g_tx_desc[i].special = 0u;
    }

    reg_write(E1000_REG_TDBAL, (uint32_t)(tx_phys & 0xFFFFFFFFu));
    reg_write(E1000_REG_TDBAH, (uint32_t)(tx_phys >> 32));
    reg_write(E1000_REG_TDLEN, (uint32_t)(sizeof(g_tx_desc)));
    reg_write(E1000_REG_TDH, 0u);
    reg_write(E1000_REG_TDT, 0u);

    reg_write(
        E1000_REG_TCTL,
        E1000_TCTL_EN |
            E1000_TCTL_PSP |
            ((uint32_t)0x10u << E1000_TCTL_CT_SHIFT) |
            ((uint32_t)0x40u << E1000_TCTL_COLD_SHIFT)
    );
    reg_write(E1000_REG_TIPG, 0x0060200Au);
}

static void init_rx_ring(void) {
    uint64_t rx_phys = (uint64_t)(uintptr_t)&g_rx_desc[0];

    for (uint32_t i = 0; i < E1000_RX_DESC_COUNT; i++) {
        g_rx_desc[i].addr = (uint64_t)(uintptr_t)&g_rx_buf[i][0];
        g_rx_desc[i].length = 0u;
        g_rx_desc[i].csum = 0u;
        g_rx_desc[i].status = 0u;
        g_rx_desc[i].errors = 0u;
        g_rx_desc[i].special = 0u;
    }

    reg_write(E1000_REG_RDBAL, (uint32_t)(rx_phys & 0xFFFFFFFFu));
    reg_write(E1000_REG_RDBAH, (uint32_t)(rx_phys >> 32));
    reg_write(E1000_REG_RDLEN, (uint32_t)(sizeof(g_rx_desc)));
    reg_write(E1000_REG_RDH, 0u);
    reg_write(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1u);

    g_e1000.rx_next = 0u;
    reg_write(E1000_REG_RCTL, E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_SECRC);
}

static int e1000_device_sync(device_t* dev) {
    (void)dev;
    return 0;
}

static const device_ops_t g_e1000_device_ops = {
    .api_version = DEVICE_OPS_API_VERSION,
    .sync = e1000_device_sync,
};

int e1000_supports_device(uint16_t vendor_id, uint16_t device_id) {
    return e1000_supports_device_internal(vendor_id, device_id);
}

uint16_t e1000_device_id(void) {
    return g_e1000.device_id;
}

int e1000_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id) {
    uint32_t bar0;
    uint16_t cmd;
    uint32_t ral;
    uint32_t rah;

    if (!e1000_supports_device_internal(E1000_VENDOR_INTEL, dev_id)) {
        return -1;
    }
    if (g_e1000.ready) {
        return 0;
    }

    mem_zero(&g_e1000, sizeof(g_e1000));
    g_e1000.pci_bus = bus;
    g_e1000.pci_slot = slot;
    g_e1000.pci_func = func;
    g_e1000.device_id = dev_id;

    cmd = pci_config_read16(bus, slot, func, 0x04u);
    cmd = (uint16_t)(cmd | 0x0006u);
    pci_config_write16(bus, slot, func, 0x04u, cmd);

    bar0 = pci_config_read32(bus, slot, func, 0x10u);
    if ((bar0 & 0x1u) != 0u) {
        return -1;
    }

    g_e1000.mmio = (volatile uint8_t*)(uintptr_t)(uint64_t)(bar0 & 0xFFFFFFF0u);
    if (!g_e1000.mmio) {
        return -1;
    }

    reg_write(E1000_REG_CTRL, reg_read(E1000_REG_CTRL) | E1000_CTRL_RST);
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((reg_read(E1000_REG_CTRL) & E1000_CTRL_RST) == 0u) {
            break;
        }
        cpu_relax();
    }

    reg_write(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)reg_read(E1000_REG_ICR);

    init_tx_ring();
    init_rx_ring();

    ral = reg_read(E1000_REG_RAL0);
    rah = reg_read(E1000_REG_RAH0);
    g_e1000.mac[0] = (uint8_t)(ral & 0xFFu);
    g_e1000.mac[1] = (uint8_t)((ral >> 8) & 0xFFu);
    g_e1000.mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
    g_e1000.mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
    g_e1000.mac[4] = (uint8_t)(rah & 0xFFu);
    g_e1000.mac[5] = (uint8_t)((rah >> 8) & 0xFFu);

    g_e1000.ready = 1u;
    (void)device_register(MYAOS_DEV_NETWORK, "eth0", "net.e1000", NULL, &g_e1000_device_ops, NULL);
    return 0;
}

int e1000_init(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t dev_id = 0;

    if (find_supported_device(&bus, &slot, &func, &dev_id) != 0) {
        return -1;
    }

    return e1000_init_pci(bus, slot, func, dev_id);
}

int e1000_ready(void) {
    return g_e1000.ready ? 1 : 0;
}

int e1000_enable_msi(uint8_t vector) {
    uint8_t irq_mode = 0u;

    if (!g_e1000.ready || !apic_is_enabled()) {
        return -1;
    }
    if (g_e1000.msi_enabled) {
        return 0;
    }

    if (pci_enable_message_signaled_irq(
            g_e1000.pci_bus,
            g_e1000.pci_slot,
            g_e1000.pci_func,
            vector,
            apic_local_id(),
            &irq_mode
        ) != 0) {
        return -1;
    }

    (void)irq_mode;
    g_e1000.msi_enabled = 1u;
    return 0;
}

int e1000_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len) {
    uint32_t attempts = E1000_RX_DESC_COUNT;
    uint8_t* out = (uint8_t*)out_buf;

    if (!out_len || (max_len != 0u && !out) || !g_e1000.ready) {
        return -1;
    }

    while (attempts-- > 0u) {
        e1000_rx_desc_t* desc = &g_rx_desc[g_e1000.rx_next];
        uint16_t len;

        if ((desc->status & E1000_RX_STATUS_DD) == 0u) {
            return -1;
        }

        len = desc->length;
        if (len > E1000_RX_BUF_SIZE) {
            len = E1000_RX_BUF_SIZE;
        }

        if (desc->errors == 0u && len > 0u) {
            uint16_t copy_len = len;
            if (copy_len > max_len) {
                copy_len = max_len;
            }
            if (copy_len > 0u) {
                mem_copy(out, &g_rx_buf[g_e1000.rx_next][0], copy_len);
            }
            *out_len = copy_len;
        } else {
            *out_len = 0u;
        }

        desc->status = 0u;
        reg_write(E1000_REG_RDT, g_e1000.rx_next);
        g_e1000.rx_next = (g_e1000.rx_next + 1u) % E1000_RX_DESC_COUNT;

        if (*out_len > 0u) {
            return 0;
        }
    }

    return -1;
}

int e1000_get_mac(uint8_t out_mac[6]) {
    if (!out_mac || !g_e1000.ready) {
        return -1;
    }

    for (uint32_t i = 0; i < 6u; i++) {
        out_mac[i] = g_e1000.mac[i];
    }
    return 0;
}

int e1000_send_frame(const void* data, uint16_t len) {
    uint32_t tail;
    e1000_tx_desc_t* desc;

    if (!g_e1000.ready || !data || len == 0u || len > 1514u) {
        return -1;
    }

    tail = reg_read(E1000_REG_TDT) % E1000_TX_DESC_COUNT;
    desc = &g_tx_desc[tail];
    if ((desc->status & E1000_TX_STATUS_DD) == 0u) {
        return -1;
    }

    mem_copy(&g_tx_buf[tail][0], data, len);
    desc->length = len;
    desc->cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    desc->status = 0u;

    reg_write(E1000_REG_TDT, (tail + 1u) % E1000_TX_DESC_COUNT);

    for (uint32_t i = 0; i < 200000u; i++) {
        if ((desc->status & E1000_TX_STATUS_DD) != 0u) {
            return 0;
        }
        cpu_relax();
    }

    return -1;
}
