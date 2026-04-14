#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include <stdint.h>

int virtio_net_init(void);
int virtio_net_init_pci(uint8_t bus, uint8_t slot, uint8_t func, uint16_t dev_id);
int virtio_net_ready(void);
int virtio_net_supports_device(uint16_t vendor_id, uint16_t device_id);
uint16_t virtio_net_device_id(void);
int virtio_net_send_frame(const void* data, uint16_t len);
int virtio_net_poll_frame(void* out_buf, uint16_t max_len, uint16_t* out_len);
int virtio_net_get_mac(uint8_t out_mac[6]);

#endif
