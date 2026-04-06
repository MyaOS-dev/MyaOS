#include "power.h"
#include "device.h"
#include <stddef.h>
#include <stdint.h>

typedef void (__attribute__((ms_abi)) *efi_reset_system_t)(
    uint32_t reset_type,
    uint64_t status,
    uint64_t data_size,
    void* data
);

typedef struct {
    boot_info_t* boot;
} power_device_ctx_t;

static power_device_ctx_t g_power_ctx;
static uint8_t g_power_registered;

static inline uint8_t port_in8(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void port_out8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void port_out16(uint16_t port, uint16_t value) {
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "Nd"(port));
}

static void firmware_reset(boot_info_t* boot, uint32_t reset_type) {
    if (!boot || boot->efi_reset_system == 0) {
        return;
    }

    efi_reset_system_t reset = (efi_reset_system_t)(uintptr_t)boot->efi_reset_system;
    reset(reset_type, 0, 0, (void*)0);
}

void power_init(boot_info_t* boot) {
    g_power_ctx.boot = boot;
    if (g_power_registered) {
        return;
    }
    if (device_register(MYAOS_DEV_POWER, "power0", "firmware", &g_power_ctx, NULL, NULL) == 0) {
        g_power_registered = 1;
    }
}

void power_shutdown(boot_info_t* boot) {
    (void)boot;

    port_out16(0x604u, 0x2000u);
    port_out16(0xB004u, 0x2000u);
    port_out16(0x4004u, 0x3400u);
}

void power_reboot(boot_info_t* boot) {
    firmware_reset(boot, 0u);

    while (port_in8(0x64u) & 0x02u) {
    }
    port_out8(0x64u, 0xFEu);
}
