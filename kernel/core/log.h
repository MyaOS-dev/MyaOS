#ifndef KLOG_H
#define KLOG_H

#include <stddef.h>
#include <stdint.h>

void klog_init(void);
void klog_write(const char* text);
void klog_write_len(const char* text, size_t len);
int klog_snapshot(char* out, uint32_t out_size, uint32_t* out_written);

#endif
