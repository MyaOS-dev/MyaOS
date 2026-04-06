#ifndef DEVICE_H
#define DEVICE_H

#include <myaos/syscall.h>
#include <stdint.h>

#define DEVICE_MAX_COUNT 16u
#define DEVICE_OPS_API_VERSION 1u

typedef struct device device_t;

typedef struct {
    uint32_t api_version;
    int (*sync)(device_t* dev);
} device_ops_t;

struct device {
    uint32_t id;
    uint32_t class_id;
    char name[MYAOS_DEVICE_NAME_MAX];
    char driver[MYAOS_DEVICE_DRIVER_MAX];
    void* ctx;
    const device_ops_t* ops;
};

void device_init(void);
int device_register(
    uint32_t class_id,
    const char* name,
    const char* driver,
    void* ctx,
    const device_ops_t* ops,
    device_t** out_dev
);
uint32_t device_count(void);
const device_t* device_get(uint32_t index);
int device_list(myaos_device_info_t* out, uint32_t max_entries, uint32_t* out_count);
int device_sync_all(void);

#endif
