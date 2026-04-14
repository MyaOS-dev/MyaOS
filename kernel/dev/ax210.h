#ifndef AX210_H
#define AX210_H

#include <stdint.h>

int ax210_init(void);
int ax210_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id);
int ax210_ready(void);
int ax210_present(void);
int ax210_firmware_found(void);
const char* ax210_firmware_state(void);
const char* ax210_firmware_path(void);
const char* ax210_state(void);
const char* ax210_note(void);
int ax210_supports_device(uint16_t vendor_id, uint16_t device_id);
uint16_t ax210_device_id(void);
int ax210_send_frame(const void* data, uint16_t len);
int ax210_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len);
int ax210_get_mac(uint8_t out_mac[6]);

#endif
