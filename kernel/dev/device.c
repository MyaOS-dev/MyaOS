#include "device.h"
#include <stddef.h>

static device_t g_devices[DEVICE_MAX_COUNT];
static uint32_t g_device_count;

static void str_copy(char* dst, const char* src, uint32_t dst_size) {
    uint32_t i = 0;

    if (dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (i + 1 < dst_size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void device_init(void) {
    for (uint32_t i = 0; i < DEVICE_MAX_COUNT; i++) {
        g_devices[i].id = 0;
        g_devices[i].class_id = MYAOS_DEV_NONE;
        g_devices[i].name[0] = '\0';
        g_devices[i].driver[0] = '\0';
        g_devices[i].ctx = NULL;
        g_devices[i].ops = NULL;
    }
    g_device_count = 0;
}

int device_register(
    uint32_t class_id,
    const char* name,
    const char* driver,
    void* ctx,
    const device_ops_t* ops,
    device_t** out_dev
) {
    if (g_device_count >= DEVICE_MAX_COUNT) {
        return -1;
    }
    if (ops && ops->api_version != DEVICE_OPS_API_VERSION) {
        return -1;
    }

    device_t* dev = &g_devices[g_device_count];
    dev->id = g_device_count;
    dev->class_id = class_id;
    str_copy(dev->name, name, sizeof(dev->name));
    str_copy(dev->driver, driver, sizeof(dev->driver));
    dev->ctx = ctx;
    dev->ops = ops;

    if (out_dev) {
        *out_dev = dev;
    }

    g_device_count++;
    return 0;
}

uint32_t device_count(void) {
    return g_device_count;
}

const device_t* device_get(uint32_t index) {
    if (index >= g_device_count) {
        return NULL;
    }
    return &g_devices[index];
}

int device_list(myaos_device_info_t* out, uint32_t max_entries, uint32_t* out_count) {
    uint32_t count = g_device_count;

    if (!out_count) {
        return -1;
    }

    if (count > max_entries) {
        count = max_entries;
    }

    for (uint32_t i = 0; i < count; i++) {
        out[i].id = g_devices[i].id;
        out[i].class_id = g_devices[i].class_id;
        str_copy(out[i].name, g_devices[i].name, sizeof(out[i].name));
        str_copy(out[i].driver, g_devices[i].driver, sizeof(out[i].driver));
    }

    *out_count = count;
    return 0;
}

int device_sync_all(void) {
    for (uint32_t i = 0; i < g_device_count; i++) {
        if (g_devices[i].ops && g_devices[i].ops->sync) {
            int rc = g_devices[i].ops->sync(&g_devices[i]);
            if (rc != 0) {
                return rc;
            }
        }
    }
    return 0;
}
