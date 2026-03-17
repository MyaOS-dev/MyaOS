#include "e1000.h"
#include "device.h"
#include "pci.h"
#include <stddef.h>

#define E1000_VENDOR_INTEL 0x8086u

#define E1000_REG_CTRL 0x0000u
#define E1000_REG_STATUS 0x0008u
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

#define E1000_TX_DESC_COUNT 16u
#define E1000_TX_BUF_SIZE 2048u
#define E1000_TX_CMD_EOP 0x01u
#define E1000_TX_CMD_IFCS 0x02u
#define E1000_TX_CMD_RS 0x08u
#define E1000_TX_STATUS_DD 0x01u

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct {
    uint8_t ready;
    uint8_t reserved0[3];
    volatile uint8_t* mmio;
    uint8_t mac[6];
} e1000_state_t;

static e1000_state_t g_e1000;
static e1000_tx_desc_t g_tx_desc[E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t g_tx_buf[E1000_TX_DESC_COUNT][E1000_TX_BUF_SIZE] __attribute__((aligned(16)));

static const uint16_t g_supported_devices[] = {
    0x100Eu,
    0x100Fu,
    0x10D3u,
    0x10EAu,
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

static int find_supported_device(uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func, uint16_t* out_dev_id) {
    for (uint32_t i = 0; i < sizeof(g_supported_devices) / sizeof(g_supported_devices[0]); i++) {
        uint8_t bus = 0;
        uint8_t slot = 0;
        uint8_t func = 0;

        if (pci_find_device(E1000_VENDOR_INTEL, g_supported_devices[i], &bus, &slot, &func) == 0) {
            if (out_bus) {
                *out_bus = bus;
            }
            if (out_slot) {
                *out_slot = slot;
            }
            if (out_func) {
                *out_func = func;
            }
            if (out_dev_id) {
                *out_dev_id = g_supported_devices[i];
            }
            return 0;
        }
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

static int e1000_device_sync(device_t* dev) {
    (void)dev;
    return 0;
}

static const device_ops_t g_e1000_device_ops = {
    .api_version = DEVICE_OPS_API_VERSION,
    .sync = e1000_device_sync,
};

int e1000_init(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t func = 0;
    uint16_t dev_id = 0;
    uint32_t bar0;
    uint16_t cmd;
    uint32_t ral;
    uint32_t rah;

    mem_zero(&g_e1000, sizeof(g_e1000));

    if (find_supported_device(&bus, &slot, &func, &dev_id) != 0) {
        return -1;
    }

    (void)dev_id;

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

int e1000_ready(void) {
    return g_e1000.ready ? 1 : 0;
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
