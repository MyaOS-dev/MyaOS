#ifndef NETNIC_H
#define NETNIC_H

#include <stdint.h>

#define NETNIC_MODULE_DRIVER_API_VERSION 1u

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
} netnic_module_driver_t;

int netnic_init(void);
int netnic_register_module_driver(const netnic_module_driver_t* driver);
int netnic_unregister_module_driver(const char* driver_name);
int netnic_ready(void);
int netnic_send_frame(const void* data, uint16_t len);
int netnic_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len);
int netnic_get_mac(uint8_t out_mac[6]);

const char* netnic_if_name(void);
const char* netnic_driver_name(void);
const char* netnic_model_name(void);
const char* netnic_state(void);
const char* netnic_note(void);
const char* netnic_firmware_state(void);
const char* netnic_firmware_path(void);
const char* netnic_pci_bdf(void);
const char* netnic_pci_id(void);
uint32_t netnic_detected_count(void);
uint32_t netnic_supported_count(void);

#endif
