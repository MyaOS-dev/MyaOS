#ifndef E1000_H
#define E1000_H

#include <stdint.h>

int e1000_init(void);
int e1000_ready(void);
int e1000_send_frame(const void* data, uint16_t len);
int e1000_get_mac(uint8_t out_mac[6]);

#endif
