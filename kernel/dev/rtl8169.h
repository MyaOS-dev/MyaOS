#ifndef RTL8169_H
#define RTL8169_H

#include <stdint.h>

int rtl8169_init(void);
int rtl8169_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id);
int rtl8169_ready(void);
int rtl8169_supports_device(uint16_t vendor_id, uint16_t device_id);
uint16_t rtl8169_device_id(void);
int rtl8169_send_frame(const void* data, uint16_t len);
int rtl8169_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len);
int rtl8169_get_mac(uint8_t out_mac[6]);

#endif
