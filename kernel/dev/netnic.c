#include "netnic.h"
#include "ax210.h"
#include "e1000.h"
#include "pci.h"
#include "rtl8139.h"
#include "rtl8169.h"
#include "virtio_net.h"
#include <myaos/syscall.h>
#include <stddef.h>

typedef enum {
    NETNIC_DRIVER_NONE = 0,
    NETNIC_DRIVER_E1000 = 1,
    NETNIC_DRIVER_RTL8139 = 2,
    NETNIC_DRIVER_RTL8169 = 3,
    NETNIC_DRIVER_VIRTIO = 4,
    NETNIC_DRIVER_AX210 = 5,
} netnic_driver_kind_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    const char* model;
    netnic_driver_kind_t driver;
} netnic_catalog_entry_t;

static const netnic_catalog_entry_t g_catalog[] = {
    {0x10ECu, 0x8139u, "Realtek RTL8139", NETNIC_DRIVER_RTL8139},
    {0x10ECu, 0x8168u, "Realtek RTL8111/8168/8211/8411 PCIe Gigabit Ethernet", NETNIC_DRIVER_RTL8169},
    {0x10ECu, 0x8169u, "Realtek RTL8169/8110 PCI Gigabit Ethernet", NETNIC_DRIVER_RTL8169},
    {0x10ECu, 0x8125u, "Realtek RTL8125", NETNIC_DRIVER_NONE},

    {0x8086u, 0x100Eu, "Intel 82540EM", NETNIC_DRIVER_E1000},
    {0x8086u, 0x10D3u, "Intel 82574L", NETNIC_DRIVER_E1000},
    {0x8086u, 0x1533u, "Intel I210", NETNIC_DRIVER_E1000},
    {0x8086u, 0x15B8u, "Intel I219", NETNIC_DRIVER_E1000},
    {0x8086u, 0x1521u, "Intel I350", NETNIC_DRIVER_E1000},
    {0x8086u, 0x10FBu, "Intel X520", NETNIC_DRIVER_NONE},
    {0x8086u, 0x1528u, "Intel X540", NETNIC_DRIVER_NONE},
    {0x8086u, 0x1572u, "Intel X710", NETNIC_DRIVER_NONE},
    {0x8086u, 0x1592u, "Intel E810", NETNIC_DRIVER_NONE},
    {0x8086u, 0x1229u, "Intel PRO 100", NETNIC_DRIVER_NONE},
    {0x8086u, 0x2725u, "Intel Wi-Fi 6E AX210/AX1675 [Typhoon Peak]", NETNIC_DRIVER_AX210},

    {0x14E4u, 0x1644u, "Broadcom BCM5700", NETNIC_DRIVER_NONE},
    {0x14E4u, 0x1657u, "Broadcom BCM5719", NETNIC_DRIVER_NONE},
    {0x14E4u, 0x165Fu, "Broadcom BCM5720", NETNIC_DRIVER_NONE},
    {0x14E4u, 0x1639u, "Broadcom NetXtreme II", NETNIC_DRIVER_NONE},
    {0x14E4u, 0x2711u, "Broadcom BCM2711 Ethernet", NETNIC_DRIVER_NONE},

    {0x11ABu, 0x4364u, "Marvell Yukon 88E8056", NETNIC_DRIVER_NONE},
    {0x11ABu, 0x1512u, "Marvell Alaska 88E1512", NETNIC_DRIVER_NONE},

    {0x1969u, 0x1063u, "Qualcomm Atheros AR8131", NETNIC_DRIVER_NONE},
    {0x1969u, 0x1091u, "Qualcomm Atheros AR8161", NETNIC_DRIVER_NONE},

    {0x15B3u, 0x1003u, "Mellanox ConnectX-3", NETNIC_DRIVER_NONE},
    {0x15B3u, 0x1013u, "Mellanox ConnectX-4", NETNIC_DRIVER_NONE},
    {0x15B3u, 0x1017u, "Mellanox ConnectX-5", NETNIC_DRIVER_NONE},
    {0x15B3u, 0xA2D2u, "NVIDIA BlueField DPU", NETNIC_DRIVER_NONE},

    {0x1425u, 0x4400u, "Chelsio T4", NETNIC_DRIVER_NONE},
    {0x1425u, 0x5400u, "Chelsio T5", NETNIC_DRIVER_NONE},
    {0x1425u, 0x6400u, "Chelsio T6", NETNIC_DRIVER_NONE},

    {0x1924u, 0x0903u, "Solarflare SFN5122F", NETNIC_DRIVER_NONE},
    {0x1924u, 0x0A03u, "Solarflare SFN7000", NETNIC_DRIVER_NONE},

    {0x0B95u, 0x7720u, "ASIX AX88772", NETNIC_DRIVER_NONE},
    {0x0B95u, 0x1790u, "ASIX AX88179", NETNIC_DRIVER_NONE},

    {0x0424u, 0x2800u, "Microchip ENC28J60", NETNIC_DRIVER_NONE},
    {0x0424u, 0x8720u, "Microchip LAN8720", NETNIC_DRIVER_NONE},
    {0x0424u, 0x9252u, "Microchip LAN9252", NETNIC_DRIVER_NONE},

    {0x16C3u, 0xABCDu, "Synopsys DesignWare MAC", NETNIC_DRIVER_NONE},
    {0x17CDu, 0xE100u, "Cadence GEM", NETNIC_DRIVER_NONE},

    {0x1AF4u, 0x1000u, "virtio-net", NETNIC_DRIVER_VIRTIO},
    {0x1AF4u, 0x1041u, "virtio-net (modern)", NETNIC_DRIVER_NONE},
    {0x15ADu, 0x07B0u, "VMware vmxnet3", NETNIC_DRIVER_NONE},
    {0x1414u, 0x0001u, "Hyper-V netvsc", NETNIC_DRIVER_NONE},

    {0x10B7u, 0x9200u, "3Com 3C905", NETNIC_DRIVER_NONE},
    {0x1011u, 0x0009u, "DEC Tulip 21140", NETNIC_DRIVER_NONE},
    {0x10ECu, 0x8029u, "NE2000", NETNIC_DRIVER_NONE},

    {0x1137u, 0x1240u, "Cisco VIC 1240", NETNIC_DRIVER_NONE},
    {0x1137u, 0x1380u, "Cisco VIC 1380", NETNIC_DRIVER_NONE},

    {0x1D0Fu, 0xEC20u, "Amazon ENA", NETNIC_DRIVER_NONE},
    {0x1AE0u, 0x0042u, "Google gVNIC", NETNIC_DRIVER_NONE},
    {0x19EEu, 0x4000u, "Netronome NFP4000", NETNIC_DRIVER_NONE},
};

static char g_active_if_name[MYAOS_DEVICE_NAME_MAX] = "none";
static char g_active_driver[MYAOS_DEVICE_DRIVER_MAX] = "none";
static char g_active_model[MYAOS_DESC_MAX] = "none";
static char g_active_pci_bdf[16] = "none";
static char g_active_pci_id[24] = "none";
static char g_probe_if_name[MYAOS_DEVICE_NAME_MAX] = "none";
static char g_probe_driver[MYAOS_DEVICE_DRIVER_MAX] = "none";
static char g_probe_model[MYAOS_DESC_MAX] = "none";
static char g_probe_pci_bdf[16] = "none";
static char g_probe_pci_id[24] = "none";
static netnic_module_driver_t g_module_drivers[16];
static uint8_t g_module_driver_used[16];
static int32_t g_active_module_driver = -1;
static uint8_t g_active_ready;
static uint8_t g_probe_ready;
static uint8_t g_probe_known;
static uint32_t g_detected_count;
static uint32_t g_supported_count;

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

static int str_eq(const char* a, const char* b) {
    size_t i = 0u;

    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i]) {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int str_starts_with(const char* text, const char* prefix) {
    size_t i = 0u;

    if (!text || !prefix) {
        return 0;
    }
    while (prefix[i]) {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int find_module_driver_slot_by_pci(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t i = 0u; i < sizeof(g_module_drivers) / sizeof(g_module_drivers[0]); i++) {
        if (!g_module_driver_used[i]) {
            continue;
        }
        if (g_module_drivers[i].vendor_id == vendor_id && g_module_drivers[i].device_id == device_id) {
            return (int)i;
        }
    }
    return -1;
}

static int find_module_driver_slot_by_name(const char* driver_name) {
    if (!driver_name || !driver_name[0]) {
        return -1;
    }
    for (uint32_t i = 0u; i < sizeof(g_module_drivers) / sizeof(g_module_drivers[0]); i++) {
        if (!g_module_driver_used[i]) {
            continue;
        }
        if (str_eq(g_module_drivers[i].driver_name, driver_name)) {
            return (int)i;
        }
    }
    return -1;
}

static const netnic_catalog_entry_t* catalog_lookup(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t i = 0; i < sizeof(g_catalog) / sizeof(g_catalog[0]); i++) {
        if (g_catalog[i].vendor_id == vendor_id && g_catalog[i].device_id == device_id) {
            return &g_catalog[i];
        }
    }
    return NULL;
}

static void format_hex_fixed(uint32_t value, uint8_t width, char* out) {
    for (uint8_t i = 0; i < width; i++) {
        uint8_t shift = (uint8_t)((width - 1u - i) * 4u);
        uint8_t nibble = (uint8_t)((value >> shift) & 0xFu);
        out[i] = (char)(nibble < 10u ? ('0' + nibble) : ('a' + (nibble - 10u)));
    }
    out[width] = '\0';
}

static void format_pci_strings(
    const pci_device_info_t* dev,
    char* out_bdf,
    size_t out_bdf_size,
    char* out_id,
    size_t out_id_size
) {
    if (!dev) {
        str_copy(out_bdf, "unknown", out_bdf_size);
        str_copy(out_id, "unknown", out_id_size);
        return;
    }

    if (out_bdf_size >= 8u) {
        char bus_hex[3];
        char slot_hex[3];
        format_hex_fixed(dev->bus, 2u, bus_hex);
        format_hex_fixed(dev->slot, 2u, slot_hex);
        out_bdf[0] = bus_hex[0];
        out_bdf[1] = bus_hex[1];
        out_bdf[2] = ':';
        out_bdf[3] = slot_hex[0];
        out_bdf[4] = slot_hex[1];
        out_bdf[5] = '.';
        out_bdf[6] = (char)('0' + (dev->func & 0x7u));
        out_bdf[7] = '\0';
    } else if (out_bdf_size > 0u) {
        out_bdf[0] = '\0';
    }

    if (out_id_size >= 17u) {
        char ven_hex[5];
        char dev_hex[5];
        char rev_hex[3];
        format_hex_fixed(dev->vendor_id, 4u, ven_hex);
        format_hex_fixed(dev->device_id, 4u, dev_hex);
        format_hex_fixed(dev->revision, 2u, rev_hex);

        out_id[0] = ven_hex[0];
        out_id[1] = ven_hex[1];
        out_id[2] = ven_hex[2];
        out_id[3] = ven_hex[3];
        out_id[4] = ':';
        out_id[5] = dev_hex[0];
        out_id[6] = dev_hex[1];
        out_id[7] = dev_hex[2];
        out_id[8] = dev_hex[3];
        out_id[9] = ' ';
        out_id[10] = 'r';
        out_id[11] = 'e';
        out_id[12] = 'v';
        out_id[13] = ' ';
        out_id[14] = rev_hex[0];
        out_id[15] = rev_hex[1];
        out_id[16] = '\0';
    } else if (out_id_size > 0u) {
        out_id[0] = '\0';
    }
}

static const char* driver_name_for_kind(netnic_driver_kind_t kind) {
    switch (kind) {
        case NETNIC_DRIVER_E1000:
            return "net.e1000";
        case NETNIC_DRIVER_RTL8139:
            return "net.rtl8139";
        case NETNIC_DRIVER_RTL8169:
            return "net.rtl8169";
        case NETNIC_DRIVER_VIRTIO:
            return "net.virtio";
        case NETNIC_DRIVER_AX210:
            return "net.ax210";
        default:
            return "net.unknown";
    }
}

static const char* if_name_for_kind(netnic_driver_kind_t kind) {
    if (kind == NETNIC_DRIVER_AX210) {
        return "wlan0";
    }
    return "eth0";
}

static void set_probe(netnic_driver_kind_t kind, const char* model, const pci_device_info_t* dev) {
    uint8_t known = (kind != NETNIC_DRIVER_NONE) ? 1u : 0u;

    if (g_probe_ready && g_probe_known && !known) {
        return;
    }
    if (!g_probe_ready || (known && !g_probe_known)) {
        str_copy(g_probe_if_name, if_name_for_kind(kind), sizeof(g_probe_if_name));
        str_copy(g_probe_driver, driver_name_for_kind(kind), sizeof(g_probe_driver));
        str_copy(g_probe_model, model ? model : "Unknown network controller", sizeof(g_probe_model));
        format_pci_strings(dev, g_probe_pci_bdf, sizeof(g_probe_pci_bdf), g_probe_pci_id, sizeof(g_probe_pci_id));
        g_probe_ready = 1u;
        g_probe_known = known;
    }
}

static void set_probe_module(const netnic_module_driver_t* drv, const pci_device_info_t* dev) {
    if (!drv) {
        return;
    }
    if (g_probe_ready && g_probe_known) {
        return;
    }
    str_copy(g_probe_if_name, drv->if_name ? drv->if_name : "eth0", sizeof(g_probe_if_name));
    str_copy(g_probe_driver, drv->driver_name ? drv->driver_name : "kmod.netnic", sizeof(g_probe_driver));
    str_copy(g_probe_model, drv->model ? drv->model : "module network controller", sizeof(g_probe_model));
    format_pci_strings(dev, g_probe_pci_bdf, sizeof(g_probe_pci_bdf), g_probe_pci_id, sizeof(g_probe_pci_id));
    g_probe_ready = 1u;
    g_probe_known = 1u;
}

static void set_active(const char* if_name, const char* driver, const char* model, const pci_device_info_t* dev) {
    str_copy(g_active_if_name, if_name, sizeof(g_active_if_name));
    str_copy(g_active_driver, driver, sizeof(g_active_driver));
    str_copy(g_active_model, model, sizeof(g_active_model));
    format_pci_strings(dev, g_active_pci_bdf, sizeof(g_active_pci_bdf), g_active_pci_id, sizeof(g_active_pci_id));
    g_active_ready = 1u;
}

static int try_initialize_module_driver(const pci_device_info_t* dev, int module_slot) {
    const netnic_module_driver_t* drv;
    int initialized;

    if (!dev || module_slot < 0 || module_slot >= (int)(sizeof(g_module_drivers) / sizeof(g_module_drivers[0]))) {
        return 0;
    }
    if (!g_module_driver_used[module_slot]) {
        return 0;
    }

    drv = &g_module_drivers[(uint32_t)module_slot];
    if (!drv->init_pci || !drv->ready || !drv->send_frame || !drv->poll_frame || !drv->get_mac) {
        return 0;
    }

    initialized = (drv->init_pci(dev->bus, dev->slot, dev->func, dev->device_id) == 0);
    if (initialized && drv->ready()) {
        set_active(
            drv->if_name ? drv->if_name : "eth0",
            drv->driver_name ? drv->driver_name : "kmod.netnic",
            drv->model ? drv->model : "module network controller",
            dev
        );
        g_active_module_driver = module_slot;
        return 1;
    }

    return 0;
}

static int is_network_controller(const pci_device_info_t* dev) {
    if (!dev) {
        return 0;
    }
    return dev->class_code == 0x02u ? 1 : 0;
}

int netnic_register_module_driver(const netnic_module_driver_t* driver) {
    int free_slot = -1;

    if (!driver || driver->api_version != NETNIC_MODULE_DRIVER_API_VERSION) {
        return -1;
    }
    if (!driver->driver_name || !driver->driver_name[0] || !driver->model || !driver->model[0] ||
        !driver->init_pci || !driver->ready || !driver->send_frame || !driver->poll_frame || !driver->get_mac) {
        return -1;
    }
    if (find_module_driver_slot_by_name(driver->driver_name) >= 0) {
        return 0;
    }

    for (uint32_t i = 0u; i < sizeof(g_module_drivers) / sizeof(g_module_drivers[0]); i++) {
        if (!g_module_driver_used[i]) {
            free_slot = (int)i;
            break;
        }
    }
    if (free_slot < 0) {
        return -1;
    }

    g_module_drivers[(uint32_t)free_slot] = *driver;
    g_module_driver_used[(uint32_t)free_slot] = 1u;
    return 0;
}

int netnic_unregister_module_driver(const char* driver_name) {
    int slot = find_module_driver_slot_by_name(driver_name);

    if (slot < 0) {
        return -1;
    }
    if (g_active_module_driver == slot && g_active_ready) {
        return -2;
    }

    g_module_drivers[(uint32_t)slot].api_version = 0u;
    g_module_drivers[(uint32_t)slot].vendor_id = 0u;
    g_module_drivers[(uint32_t)slot].device_id = 0u;
    g_module_drivers[(uint32_t)slot].model = NULL;
    g_module_drivers[(uint32_t)slot].if_name = NULL;
    g_module_drivers[(uint32_t)slot].driver_name = NULL;
    g_module_drivers[(uint32_t)slot].init_pci = NULL;
    g_module_drivers[(uint32_t)slot].ready = NULL;
    g_module_drivers[(uint32_t)slot].send_frame = NULL;
    g_module_drivers[(uint32_t)slot].poll_frame = NULL;
    g_module_drivers[(uint32_t)slot].get_mac = NULL;
    g_module_driver_used[(uint32_t)slot] = 0u;
    return 0;
}

int netnic_init(void) {
    pci_device_info_t entries[128];
    uint32_t count = 0;

    g_active_ready = 0u;
    g_probe_ready = 0u;
    g_probe_known = 0u;
    g_active_module_driver = -1;
    g_detected_count = 0u;
    g_supported_count = 0u;
    str_copy(g_active_if_name, "none", sizeof(g_active_if_name));
    str_copy(g_active_driver, "none", sizeof(g_active_driver));
    str_copy(g_active_model, "none", sizeof(g_active_model));
    str_copy(g_active_pci_bdf, "none", sizeof(g_active_pci_bdf));
    str_copy(g_active_pci_id, "none", sizeof(g_active_pci_id));
    str_copy(g_probe_if_name, "none", sizeof(g_probe_if_name));
    str_copy(g_probe_driver, "none", sizeof(g_probe_driver));
    str_copy(g_probe_model, "none", sizeof(g_probe_model));
    str_copy(g_probe_pci_bdf, "none", sizeof(g_probe_pci_bdf));
    str_copy(g_probe_pci_id, "none", sizeof(g_probe_pci_id));

    if (pci_scan_devices(entries, (uint32_t)(sizeof(entries) / sizeof(entries[0])), &count) < 0) {
        count = 0u;
    }

    for (uint32_t i = 0; i < count; i++) {
        const netnic_catalog_entry_t* info;
        int initialized = 0;
        int module_slot;
        uint8_t supported_for_device = 0u;

        if (!is_network_controller(&entries[i])) {
            continue;
        }

        g_detected_count++;
        info = catalog_lookup(entries[i].vendor_id, entries[i].device_id);
        module_slot = find_module_driver_slot_by_pci(entries[i].vendor_id, entries[i].device_id);

        if (info && info->driver != NETNIC_DRIVER_NONE) {
            set_probe(info->driver, info->model, &entries[i]);
            supported_for_device = 1u;
        } else if (module_slot >= 0) {
            set_probe_module(&g_module_drivers[(uint32_t)module_slot], &entries[i]);
            supported_for_device = 1u;
        } else if (info) {
            set_probe(info->driver, info->model, &entries[i]);
        } else {
            set_probe(NETNIC_DRIVER_NONE, "Unknown network controller", &entries[i]);
        }

        if (!supported_for_device &&
            (e1000_supports_device(entries[i].vendor_id, entries[i].device_id) ||
             rtl8139_supports_device(entries[i].vendor_id, entries[i].device_id) ||
             rtl8169_supports_device(entries[i].vendor_id, entries[i].device_id) ||
             virtio_net_supports_device(entries[i].vendor_id, entries[i].device_id) ||
             ax210_supports_device(entries[i].vendor_id, entries[i].device_id))) {
            supported_for_device = 1u;
        }
        if (supported_for_device) {
            g_supported_count++;
        }

        if (g_active_ready) {
            continue;
        }

        if (info && info->driver == NETNIC_DRIVER_E1000) {
            initialized = (e1000_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && e1000_ready()) {
                set_active("eth0", "net.e1000", info->model, &entries[i]);
            }
        } else if (info && info->driver == NETNIC_DRIVER_RTL8139) {
            initialized =
                (rtl8139_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && rtl8139_ready()) {
                set_active("eth0", "net.rtl8139", info->model, &entries[i]);
            }
        } else if (info && info->driver == NETNIC_DRIVER_RTL8169) {
            initialized =
                (rtl8169_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && rtl8169_ready()) {
                set_active("eth0", "net.rtl8169", info->model, &entries[i]);
            }
        } else if (info && info->driver == NETNIC_DRIVER_VIRTIO) {
            initialized =
                (virtio_net_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && virtio_net_ready()) {
                set_active("eth0", "net.virtio", info->model, &entries[i]);
            }
        } else if (info && info->driver == NETNIC_DRIVER_AX210) {
            initialized = (ax210_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && ax210_ready()) {
                set_active("wlan0", "net.ax210", info->model, &entries[i]);
            }
        } else if (module_slot >= 0 && try_initialize_module_driver(&entries[i], module_slot)) {
            initialized = 1;
        } else if (e1000_supports_device(entries[i].vendor_id, entries[i].device_id)) {
            initialized = (e1000_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && e1000_ready()) {
                set_active("eth0", "net.e1000", "Intel e1000-compatible", &entries[i]);
            }
        } else if (rtl8139_supports_device(entries[i].vendor_id, entries[i].device_id)) {
            initialized = (rtl8139_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && rtl8139_ready()) {
                set_active("eth0", "net.rtl8139", "Realtek RTL8139-compatible", &entries[i]);
            }
        } else if (rtl8169_supports_device(entries[i].vendor_id, entries[i].device_id)) {
            initialized = (rtl8169_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && rtl8169_ready()) {
                set_active("eth0", "net.rtl8169", "Realtek RTL8169-compatible", &entries[i]);
            }
        } else if (virtio_net_supports_device(entries[i].vendor_id, entries[i].device_id)) {
            initialized =
                (virtio_net_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && virtio_net_ready()) {
                set_active("eth0", "net.virtio", "virtio-net", &entries[i]);
            }
        } else if (ax210_supports_device(entries[i].vendor_id, entries[i].device_id)) {
            initialized = (ax210_init_pci(entries[i].bus, entries[i].slot, entries[i].func, entries[i].device_id) == 0);
            if (initialized && ax210_ready()) {
                set_active("wlan0", "net.ax210", "Intel AX210-compatible", &entries[i]);
            }
        }
    }

    if (!g_active_ready && e1000_init() == 0 && e1000_ready()) {
        const netnic_catalog_entry_t* info = catalog_lookup(0x8086u, e1000_device_id());
        set_active("eth0", "net.e1000", info ? info->model : "Intel e1000-compatible", NULL);
    }
    if (!g_active_ready && rtl8139_init() == 0 && rtl8139_ready()) {
        const netnic_catalog_entry_t* info = catalog_lookup(0x10ECu, rtl8139_device_id());
        set_active("eth0", "net.rtl8139", info ? info->model : "Realtek RTL8139-compatible", NULL);
    }
    if (!g_active_ready && rtl8169_init() == 0 && rtl8169_ready()) {
        const netnic_catalog_entry_t* info = catalog_lookup(0x10ECu, rtl8169_device_id());
        set_active("eth0", "net.rtl8169", info ? info->model : "Realtek RTL8169-compatible", NULL);
    }
    if (!g_active_ready && virtio_net_init() == 0 && virtio_net_ready()) {
        const netnic_catalog_entry_t* info = catalog_lookup(0x1AF4u, virtio_net_device_id());
        set_active("eth0", "net.virtio", info ? info->model : "virtio-net", NULL);
    }
    if (!g_active_ready && ax210_init() == 0 && ax210_ready()) {
        const netnic_catalog_entry_t* info = catalog_lookup(0x8086u, ax210_device_id());
        set_active("wlan0", "net.ax210", info ? info->model : "Intel AX210-compatible", NULL);
    }
    if (g_active_ready && g_supported_count == 0u) {
        g_supported_count = 1u;
    }

    return g_active_ready ? 0 : -1;
}

int netnic_ready(void) {
    return g_active_ready ? 1 : 0;
}

int netnic_send_frame(const void* data, uint16_t len) {
    if (g_active_module_driver >= 0 &&
        g_module_driver_used[(uint32_t)g_active_module_driver] &&
        g_module_drivers[(uint32_t)g_active_module_driver].ready &&
        g_module_drivers[(uint32_t)g_active_module_driver].ready()) {
        return g_module_drivers[(uint32_t)g_active_module_driver].send_frame(data, len);
    }
    if (e1000_ready()) {
        return e1000_send_frame(data, len);
    }
    if (rtl8139_ready()) {
        return rtl8139_send_frame(data, len);
    }
    if (rtl8169_ready()) {
        return rtl8169_send_frame(data, len);
    }
    if (virtio_net_ready()) {
        return virtio_net_send_frame(data, len);
    }
    if (ax210_ready()) {
        return ax210_send_frame(data, len);
    }
    return -1;
}

int netnic_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len) {
    if (g_active_module_driver >= 0 &&
        g_module_driver_used[(uint32_t)g_active_module_driver] &&
        g_module_drivers[(uint32_t)g_active_module_driver].ready &&
        g_module_drivers[(uint32_t)g_active_module_driver].ready()) {
        return g_module_drivers[(uint32_t)g_active_module_driver].poll_frame(out_buf, max_len, out_len);
    }
    if (e1000_ready()) {
        return e1000_poll_frame(out_buf, max_len, out_len);
    }
    if (rtl8139_ready()) {
        return rtl8139_poll_frame(out_buf, max_len, out_len);
    }
    if (rtl8169_ready()) {
        return rtl8169_poll_frame(out_buf, max_len, out_len);
    }
    if (virtio_net_ready()) {
        return virtio_net_poll_frame(out_buf, max_len, out_len);
    }
    if (ax210_ready()) {
        return ax210_poll_frame(out_buf, max_len, out_len);
    }
    return -1;
}

int netnic_get_mac(uint8_t out_mac[6]) {
    if (g_active_module_driver >= 0 &&
        g_module_driver_used[(uint32_t)g_active_module_driver] &&
        g_module_drivers[(uint32_t)g_active_module_driver].ready &&
        g_module_drivers[(uint32_t)g_active_module_driver].ready()) {
        return g_module_drivers[(uint32_t)g_active_module_driver].get_mac(out_mac);
    }
    if (e1000_ready()) {
        return e1000_get_mac(out_mac);
    }
    if (rtl8139_ready()) {
        return rtl8139_get_mac(out_mac);
    }
    if (rtl8169_ready()) {
        return rtl8169_get_mac(out_mac);
    }
    if (virtio_net_ready()) {
        return virtio_net_get_mac(out_mac);
    }
    if (ax210_ready()) {
        return ax210_get_mac(out_mac);
    }
    return -1;
}

const char* netnic_if_name(void) {
    return g_active_ready ? g_active_if_name : g_probe_if_name;
}

const char* netnic_driver_name(void) {
    return g_active_ready ? g_active_driver : g_probe_driver;
}

const char* netnic_model_name(void) {
    return g_active_ready ? g_active_model : g_probe_model;
}

const char* netnic_state(void) {
    if (g_active_ready) {
        return "ready";
    }
    if (str_starts_with(g_probe_driver, "kmod.")) {
        return "module-probe";
    }
    if (ax210_present()) {
        return ax210_state();
    }
    if (g_detected_count > 0u) {
        return "unsupported";
    }
    return "none";
}

const char* netnic_note(void) {
    if (g_active_ready) {
        return "data path active";
    }
    if (str_starts_with(g_probe_driver, "kmod.")) {
        return "module driver is registered, but device is not initialized";
    }
    if (ax210_present()) {
        return ax210_note();
    }
    if (g_detected_count > 0u) {
        return "detected network controller has no active driver";
    }
    return "no network controller detected";
}

const char* netnic_firmware_state(void) {
    if (ax210_present()) {
        return ax210_firmware_state();
    }
    return "n/a";
}

const char* netnic_firmware_path(void) {
    if (ax210_present()) {
        return ax210_firmware_path();
    }
    return "none";
}

const char* netnic_pci_bdf(void) {
    return g_active_ready ? g_active_pci_bdf : g_probe_pci_bdf;
}

const char* netnic_pci_id(void) {
    return g_active_ready ? g_active_pci_id : g_probe_pci_id;
}

uint32_t netnic_detected_count(void) {
    return g_detected_count;
}

uint32_t netnic_supported_count(void) {
    return g_supported_count;
}
