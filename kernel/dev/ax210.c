#include "ax210.h"
#include "device.h"
#include "pci.h"
#include "vfs.h"
#include <stddef.h>

#define AX210_VENDOR_INTEL 0x8086u
#define AX210_FW_PATH_MAX 96u

typedef struct {
    uint8_t present;
    uint8_t ready;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint8_t has_mmio;
    uint8_t fw_present;
    uint8_t fw_probe_attempted;
    uint16_t device_id;
    uint16_t reserved0;
    uint64_t mmio_base;
    volatile uint8_t* mmio;
    char fw_path[AX210_FW_PATH_MAX];
} ax210_state_t;

static ax210_state_t g_ax210;

static const uint16_t g_supported_devices[] = {
    0x2725u,
};

static const char* g_fw_paths[] = {
    "/lib/firmware/ax210.ucode",
    "/lib/firmware/iwlwifi-ty-a0-gf-a0.ucode",
    "/assets/ax210.ucode",
};

static void mem_zero(void* ptr, size_t size) {
    uint8_t* out = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        out[i] = 0u;
    }
}

static void str_copy(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;

    if (!dst || dst_size == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (src[i] && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ax210_probe_firmware(void) {
    uint8_t probe = 0u;
    uint32_t read_size = 0u;

    if (!g_ax210.present || g_ax210.fw_present) {
        return;
    }

    g_ax210.fw_probe_attempted = 1u;
    g_ax210.fw_path[0] = '\0';
    for (uint32_t i = 0; i < sizeof(g_fw_paths) / sizeof(g_fw_paths[0]); i++) {
        if (vfs_read_file("/", g_fw_paths[i], &probe, 1u, &read_size) == 0) {
            g_ax210.fw_present = 1u;
            str_copy(g_ax210.fw_path, g_fw_paths[i], sizeof(g_ax210.fw_path));
            return;
        }
    }
}

static int ax210_supports_device_internal(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != AX210_VENDOR_INTEL) {
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
    uint32_t count = 0u;

    if (pci_scan_devices(entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])), &count) < 0) {
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        if (!ax210_supports_device_internal(entries[i].vendor_id, entries[i].device_id)) {
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

static int ax210_device_sync(device_t* dev) {
    (void)dev;
    ax210_probe_firmware();
    return 0;
}

static const device_ops_t g_ax210_device_ops = {
    .api_version = DEVICE_OPS_API_VERSION,
    .sync = ax210_device_sync,
};

int ax210_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id) {
    uint16_t cmd;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar_type;
    uint64_t mmio_base;

    if (!ax210_supports_device_internal(AX210_VENDOR_INTEL, dev_id)) {
        return -1;
    }
    if (g_ax210.present) {
        return 0;
    }

    mem_zero(&g_ax210, sizeof(g_ax210));
    g_ax210.pci_bus = bus;
    g_ax210.pci_slot = slot;
    g_ax210.pci_func = func;
    g_ax210.device_id = dev_id;
    g_ax210.present = 1u;

    cmd = pci_config_read16(bus, slot, func, 0x04u);
    cmd = (uint16_t)(cmd | 0x0006u);
    pci_config_write16(bus, slot, func, 0x04u, cmd);

    bar0 = pci_config_read32(bus, slot, func, 0x10u);
    if ((bar0 & 0x1u) == 0u) {
        bar_type = (bar0 >> 1) & 0x3u;
        mmio_base = (uint64_t)(bar0 & 0xFFFFFFF0u);
        if (bar_type == 0x2u) {
            bar1 = pci_config_read32(bus, slot, func, 0x14u);
            mmio_base |= ((uint64_t)bar1 << 32);
        }
        if (mmio_base != 0u) {
            g_ax210.mmio_base = mmio_base;
            g_ax210.mmio = (volatile uint8_t*)(uintptr_t)mmio_base;
            g_ax210.has_mmio = 1u;
        }
    }

    ax210_probe_firmware();

    /* Data path is intentionally disabled until full 802.11 + firmware flow exists. */
    g_ax210.ready = 0u;
    (void)device_register(MYAOS_DEV_NETWORK, "wlan0", "net.ax210", NULL, &g_ax210_device_ops, NULL);
    return 0;
}

int ax210_init(void) {
    uint8_t bus = 0u;
    uint8_t slot = 0u;
    uint8_t func = 0u;
    uint16_t dev_id = 0u;

    if (find_supported_device(&bus, &slot, &func, &dev_id) != 0) {
        return -1;
    }

    return ax210_init_pci(bus, slot, func, dev_id);
}

int ax210_ready(void) {
    return g_ax210.ready ? 1 : 0;
}

int ax210_present(void) {
    return g_ax210.present ? 1 : 0;
}

int ax210_firmware_found(void) {
    ax210_probe_firmware();
    return g_ax210.fw_present ? 1 : 0;
}

const char* ax210_firmware_state(void) {
    if (!g_ax210.present) {
        return "n/a";
    }
    ax210_probe_firmware();
    return g_ax210.fw_present ? "present" : "missing";
}

const char* ax210_firmware_path(void) {
    if (!g_ax210.present) {
        return "none";
    }
    ax210_probe_firmware();
    if (g_ax210.fw_present && g_ax210.fw_path[0]) {
        return g_ax210.fw_path;
    }
    return "none";
}

const char* ax210_state(void) {
    if (!g_ax210.present) {
        return "absent";
    }
    if (g_ax210.ready) {
        return "ready";
    }
    if (!g_ax210.has_mmio) {
        return "pci-no-mmio";
    }
    return ax210_firmware_found() ? "fw-present" : "fw-missing";
}

const char* ax210_note(void) {
    if (!g_ax210.present) {
        return "AX210 adapter not detected";
    }
    if (g_ax210.ready) {
        return "AX210 data path active";
    }
    if (!g_ax210.has_mmio) {
        return "AX210 BAR0 MMIO is unavailable";
    }
    ax210_probe_firmware();
    if (g_ax210.fw_present) {
        return "firmware found, but 802.11 data path is not implemented yet";
    }
    return "firmware missing: add /lib/firmware/ax210.ucode or /lib/firmware/iwlwifi-ty-a0-gf-a0.ucode";
}

int ax210_supports_device(uint16_t vendor_id, uint16_t device_id) {
    return ax210_supports_device_internal(vendor_id, device_id);
}

uint16_t ax210_device_id(void) {
    return g_ax210.device_id;
}

int ax210_send_frame(const void* data, uint16_t len) {
    (void)data;
    (void)len;
    return -1;
}

int ax210_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len) {
    (void)out_buf;
    (void)max_len;

    if (out_len) {
        *out_len = 0u;
    }
    return -1;
}

int ax210_get_mac(uint8_t out_mac[6]) {
    (void)out_mac;
    return -1;
}
