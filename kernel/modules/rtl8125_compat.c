#include <myaos/kmod.h>
#include "rtl8169.h"

static int rtl8125_mod_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t device_id) {
    return rtl8169_init_pci(bus, slot, func, device_id);
}

static int rtl8125_mod_ready(void) {
    return rtl8169_ready();
}

static int rtl8125_mod_send_frame(const void* data, uint16_t len) {
    return rtl8169_send_frame(data, len);
}

static int rtl8125_mod_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len) {
    return rtl8169_poll_frame(out_buf, max_len, out_len);
}

static int rtl8125_mod_get_mac(uint8_t out_mac[6]) {
    return rtl8169_get_mac(out_mac);
}

static int rtl8125_mod_on_load(const myaos_kmod_api_t* api) {
    static const myaos_kmod_netnic_driver_t drv = {
        .api_version = MYAOS_KMOD_NETNIC_DRIVER_API_VERSION,
        .vendor_id = 0x10ECu,
        .device_id = 0x8125u,
        .model = "Realtek RTL8125 (compat via rtl8169)",
        .if_name = "eth0",
        .driver_name = "kmod.rtl8125",
        .init_pci = rtl8125_mod_init_pci,
        .ready = rtl8125_mod_ready,
        .send_frame = rtl8125_mod_send_frame,
        .poll_frame = rtl8125_mod_poll_frame,
        .get_mac = rtl8125_mod_get_mac,
    };

    if (!api || !api->register_netnic_driver) {
        return -1;
    }
    if (api->register_netnic_driver(&drv) != 0) {
        return -1;
    }
    if (api->net_reprobe) {
        (void)api->net_reprobe();
    }
    return 0;
}

static int rtl8125_mod_on_unload(const myaos_kmod_api_t* api) {
    if (!api || !api->unregister_netnic_driver) {
        return -1;
    }
    return api->unregister_netnic_driver("kmod.rtl8125");
}

MYAOS_KMOD_DECLARE(
    g_rtl8125_compat_module,
    ((myaos_kmod_descriptor_t){
        .abi_version = MYAOS_KMOD_ABI_VERSION,
        .name = "rtl8125",
        .description = "Realtek RTL8125 compatibility NIC module",
        .optional = 1u,
        .autoload = 1u,
        .reserved0 = 0u,
        .on_load = rtl8125_mod_on_load,
        .on_unload = rtl8125_mod_on_unload,
    })
);
