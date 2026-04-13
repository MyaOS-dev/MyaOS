#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

int rtl8139_init(void);
int rtl8139_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id);
int rtl8139_ready(void);
int rtl8139_supports_device(uint16_t vendor_id, uint16_t device_id);
uint16_t rtl8139_device_id(void);
int rtl8139_send_frame(const void* data, uint16_t len);
int rtl8139_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len);
int rtl8139_get_mac(uint8_t out_mac[6]);

#endif
