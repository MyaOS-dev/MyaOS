#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include "boot.h"
#include <stdint.h>

#define KERNEL_INIT_MAX_STEPS 64u

typedef int (*kernel_init_fn_t)(boot_info_t* boot);

typedef struct {
    const char* name;
    kernel_init_fn_t fn;
} kernel_init_step_t;

typedef struct {
    char name[32];
    int32_t rc;
    uint8_t state;
} kernel_init_step_status_t;

enum {
    KERNEL_INIT_PENDING = 0,
    KERNEL_INIT_OK = 1,
    KERNEL_INIT_FAILED = 2,
};

void kernel_init_reset(void);
int kernel_init_run(boot_info_t* boot, const kernel_init_step_t* steps, uint32_t step_count);

uint32_t kernel_init_step_count(void);
uint32_t kernel_init_ok_count(void);
const char* kernel_init_last_step(void);
const char* kernel_init_failed_step(void);
int32_t kernel_init_failed_rc(void);
int kernel_init_step_status(uint32_t index, kernel_init_step_status_t* out);

#endif
