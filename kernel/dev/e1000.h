#ifndef E1000_H
#define E1000_H

#include <stdint.h>

int e1000_init(void);
int e1000_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id);
int e1000_ready(void);
int e1000_supports_device(uint16_t vendor_id, uint16_t device_id);
uint16_t e1000_device_id(void);
int e1000_enable_msi(uint8_t vector);
int e1000_send_frame(const void* data, uint16_t len);
int e1000_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len);
int e1000_get_mac(uint8_t out_mac[6]);

#endif
