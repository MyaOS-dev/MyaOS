#include "init.h"
#include <stddef.h>

static kernel_init_step_status_t g_steps[KERNEL_INIT_MAX_STEPS];
static uint32_t g_step_count;
static uint32_t g_ok_count;
static int32_t g_failed_rc;
static char g_last_step[32];
static char g_failed_step[32];

static void str_copy(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;

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

void kernel_init_reset(void) {
    for (uint32_t i = 0; i < KERNEL_INIT_MAX_STEPS; i++) {
        g_steps[i].name[0] = '\0';
        g_steps[i].rc = 0;
        g_steps[i].state = KERNEL_INIT_PENDING;
    }
    g_step_count = 0;
    g_ok_count = 0;
    g_failed_rc = 0;
    g_last_step[0] = '\0';
    g_failed_step[0] = '\0';
}

int kernel_init_run(boot_info_t* boot, const kernel_init_step_t* steps, uint32_t step_count) {
    if (!steps || step_count == 0u || step_count > KERNEL_INIT_MAX_STEPS) {
        return -1;
    }

    kernel_init_reset();
    g_step_count = step_count;

    for (uint32_t i = 0; i < step_count; i++) {
        str_copy(g_steps[i].name, steps[i].name ? steps[i].name : "step", sizeof(g_steps[i].name));
    }

    for (uint32_t i = 0; i < step_count; i++) {
        int32_t rc = -1;

        str_copy(g_last_step, g_steps[i].name, sizeof(g_last_step));
        if (steps[i].fn) {
            rc = (int32_t)steps[i].fn(boot);
        }

        g_steps[i].rc = rc;
        if (rc == 0) {
            g_steps[i].state = KERNEL_INIT_OK;
            g_ok_count++;
            continue;
        }

        g_steps[i].state = KERNEL_INIT_FAILED;
        g_failed_rc = rc;
        str_copy(g_failed_step, g_steps[i].name, sizeof(g_failed_step));
        return -1;
    }

    return 0;
}

uint32_t kernel_init_step_count(void) {
    return g_step_count;
}

uint32_t kernel_init_ok_count(void) {
    return g_ok_count;
}

const char* kernel_init_last_step(void) {
    return g_last_step;
}

const char* kernel_init_failed_step(void) {
    return g_failed_step;
}

int32_t kernel_init_failed_rc(void) {
    return g_failed_rc;
}

int kernel_init_step_status(uint32_t index, kernel_init_step_status_t* out) {
    if (!out || index >= g_step_count) {
        return -1;
    }

    *out = g_steps[index];
    return 0;
}
