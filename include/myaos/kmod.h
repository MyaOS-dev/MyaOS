#ifndef MYAOS_KMOD_H
#define MYAOS_KMOD_H

#include <stdint.h>

#define MYAOS_KMOD_ABI_VERSION 1u
#define MYAOS_KMOD_NETNIC_DRIVER_API_VERSION 1u

typedef struct {
    uint32_t api_version;
    uint16_t vendor_id;
    uint16_t device_id;
    const char* model;
    const char* if_name;
    const char* driver_name;
    int (*init_pci)(uint8_t bus, uint8_t slot, uint8_t func, uint16_t device_id);
    int (*ready)(void);
    int (*send_frame)(const void* data, uint16_t len);
    int (*poll_frame)(void* out_buf, uint16_t max_len, uint16_t* out_len);
    int (*get_mac)(uint8_t out_mac[6]);
} myaos_kmod_netnic_driver_t;

typedef struct myaos_kmod_api {
    uint32_t abi_version;
    int (*register_netnic_driver)(const myaos_kmod_netnic_driver_t* driver);
    int (*unregister_netnic_driver)(const char* driver_name);
    int (*net_reprobe)(void);
} myaos_kmod_api_t;

typedef int (*myaos_kmod_hook_t)(const myaos_kmod_api_t* api);

typedef struct {
    uint32_t abi_version;
    const char* name;
    const char* description;
    uint8_t optional;
    uint8_t autoload;
    uint16_t reserved0;
    myaos_kmod_hook_t on_load;
    myaos_kmod_hook_t on_unload;
} myaos_kmod_descriptor_t;

#define MYAOS_KMOD_DECLARE(symbol, descriptor_literal) \
    __attribute__((used, section(".myaos_kmods"))) const myaos_kmod_descriptor_t symbol = descriptor_literal

#endif
