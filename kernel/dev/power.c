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

typedef uint64_t (__attribute__((ms_abi)) *efi_get_variable_t)(
    uint16_t* variable_name,
    const void* vendor_guid,
    uint32_t* attributes,
    uint64_t* data_size,
    void* data
);

typedef uint64_t (__attribute__((ms_abi)) *efi_set_variable_t)(
    uint16_t* variable_name,
    const void* vendor_guid,
    uint32_t attributes,
    uint64_t data_size,
    const void* data
);

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} efi_guid_t;

typedef struct {
    boot_info_t* boot;
} power_device_ctx_t;

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
    uint8_t address_space_id;
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
} acpi_gas_t;

static power_device_ctx_t g_power_ctx;
static uint8_t g_power_registered;
static const efi_guid_t g_gop_pref_var_guid = { 0x3f9f2e7a, 0x5f19, 0x4d7a, { 0xa6, 0x6c, 0x61, 0x98, 0x59, 0x2d, 0xb2, 0x41 } };
static uint16_t g_gop_pref_var_name[] = {
    'M', 'y', 'a', 'O', 'S', 'G', 'o', 'p', 'M', 'o', 'd', 'e', 0
};

#define EFI_RESET_SHUTDOWN 2u
#define ACPI_SIG_RSDP "RSD PTR "
#define ACPI_SIG_XSDT "XSDT"
#define ACPI_SIG_RSDT "RSDT"
#define ACPI_SIG_FADT "FACP"
#define ACPI_SIG_DSDT "DSDT"
#define ACPI_SPACE_SYSTEM_IO 1u
#define ACPI_SLP_EN (1u << 13)
#define ACPI_FADT_OFF_DSDT 40u
#define ACPI_FADT_OFF_PM1A_CNT_BLK 64u
#define ACPI_FADT_OFF_PM1B_CNT_BLK 68u
#define ACPI_FADT_OFF_PM1_CNT_LEN 89u
#define ACPI_FADT_OFF_X_DSDT 140u
#define ACPI_FADT_OFF_X_PM1A_CNT_BLK 188u
#define ACPI_FADT_OFF_X_PM1B_CNT_BLK 200u

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

static uint8_t bytes_eq(const uint8_t* a, const char* b, uint32_t len) {
    if (!a || !b) {
        return 0u;
    }
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != (uint8_t)b[i]) {
            return 0u;
        }
    }
    return 1u;
}

static uint32_t read_u32_le(const uint8_t* data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t read_u64_le(const uint8_t* data) {
    return (uint64_t)data[0] |
           ((uint64_t)data[1] << 8) |
           ((uint64_t)data[2] << 16) |
           ((uint64_t)data[3] << 24) |
           ((uint64_t)data[4] << 32) |
           ((uint64_t)data[5] << 40) |
           ((uint64_t)data[6] << 48) |
           ((uint64_t)data[7] << 56);
}

static uint8_t acpi_checksum_ok(const void* table, uint32_t length) {
    const uint8_t* bytes = (const uint8_t*)table;
    uint8_t sum = 0u;

    if (!bytes || length == 0u) {
        return 0u;
    }

    for (uint32_t i = 0; i < length; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0u ? 1u : 0u;
}

static const acpi_rsdp_t* acpi_get_rsdp(const boot_info_t* boot) {
    const acpi_rsdp_t* rsdp;

    if (!boot || boot->acpi_rsdp == 0u) {
        return NULL;
    }

    rsdp = (const acpi_rsdp_t*)(uintptr_t)boot->acpi_rsdp;
    if (!bytes_eq((const uint8_t*)rsdp->signature, ACPI_SIG_RSDP, 8u)) {
        return NULL;
    }
    if (!acpi_checksum_ok(rsdp, 20u)) {
        return NULL;
    }
    if (rsdp->revision >= 2u) {
        uint32_t length = rsdp->length;
        if (length < sizeof(acpi_rsdp_t)) {
            return NULL;
        }
        if (!acpi_checksum_ok(rsdp, length)) {
            return NULL;
        }
    }

    return rsdp;
}

static uint8_t acpi_sdt_valid(const acpi_sdt_header_t* header, const char* signature) {
    if (!header || header->length < sizeof(acpi_sdt_header_t)) {
        return 0u;
    }
    if (signature && !bytes_eq((const uint8_t*)header->signature, signature, 4u)) {
        return 0u;
    }
    if (!acpi_checksum_ok(header, header->length)) {
        return 0u;
    }
    return 1u;
}

static const acpi_sdt_header_t* acpi_find_table(const boot_info_t* boot, const char* signature) {
    const acpi_rsdp_t* rsdp = acpi_get_rsdp(boot);

    if (!rsdp || !signature) {
        return NULL;
    }

    if ((boot->acpi_revision >= 2u || rsdp->revision >= 2u) && rsdp->xsdt_address != 0u) {
        const acpi_sdt_header_t* xsdt = (const acpi_sdt_header_t*)(uintptr_t)rsdp->xsdt_address;
        if (acpi_sdt_valid(xsdt, ACPI_SIG_XSDT)) {
            const uint8_t* entries = (const uint8_t*)xsdt + sizeof(acpi_sdt_header_t);
            uint32_t entry_bytes = xsdt->length - (uint32_t)sizeof(acpi_sdt_header_t);
            uint32_t count = entry_bytes / 8u;
            for (uint32_t i = 0; i < count; i++) {
                uint64_t addr = read_u64_le(entries + i * 8u);
                const acpi_sdt_header_t* table = (const acpi_sdt_header_t*)(uintptr_t)addr;
                if (acpi_sdt_valid(table, signature)) {
                    return table;
                }
            }
        }
    }

    if (rsdp->rsdt_address != 0u) {
        const acpi_sdt_header_t* rsdt = (const acpi_sdt_header_t*)(uintptr_t)(uint64_t)rsdp->rsdt_address;
        if (acpi_sdt_valid(rsdt, ACPI_SIG_RSDT)) {
            const uint8_t* entries = (const uint8_t*)rsdt + sizeof(acpi_sdt_header_t);
            uint32_t entry_bytes = rsdt->length - (uint32_t)sizeof(acpi_sdt_header_t);
            uint32_t count = entry_bytes / 4u;
            for (uint32_t i = 0; i < count; i++) {
                uint64_t addr = (uint64_t)read_u32_le(entries + i * 4u);
                const acpi_sdt_header_t* table = (const acpi_sdt_header_t*)(uintptr_t)addr;
                if (acpi_sdt_valid(table, signature)) {
                    return table;
                }
            }
        }
    }

    return NULL;
}

static int aml_parse_pkg_length(const uint8_t* data, uint32_t length, uint32_t* out_value, uint32_t* out_used) {
    uint8_t lead;
    uint32_t follow;
    uint32_t value;

    if (!data || !out_used || length == 0u) {
        return -1;
    }

    lead = data[0];
    follow = (uint32_t)((lead >> 6) & 0x3u);
    if (follow + 1u > length) {
        return -1;
    }

    if (follow == 0u) {
        if (out_value) {
            *out_value = (uint32_t)(lead & 0x3Fu);
        }
        *out_used = 1u;
        return 0;
    }

    value = (uint32_t)(lead & 0x0Fu);
    for (uint32_t i = 0; i < follow; i++) {
        value |= (uint32_t)data[1u + i] << (4u + i * 8u);
    }
    if (out_value) {
        *out_value = value;
    }
    *out_used = follow + 1u;
    return 0;
}

static int aml_parse_integer(const uint8_t* data, uint32_t length, uint64_t* out_value, uint32_t* out_used) {
    if (!data || !out_value || !out_used || length == 0u) {
        return -1;
    }

    switch (data[0]) {
    case 0x00u:
        *out_value = 0u;
        *out_used = 1u;
        return 0;
    case 0x01u:
        *out_value = 1u;
        *out_used = 1u;
        return 0;
    case 0xFFu:
        *out_value = 0xFFFFFFFFFFFFFFFFULL;
        *out_used = 1u;
        return 0;
    case 0x0Au:
        if (length < 2u) {
            return -1;
        }
        *out_value = data[1];
        *out_used = 2u;
        return 0;
    case 0x0Bu:
        if (length < 3u) {
            return -1;
        }
        *out_value = (uint64_t)data[1] | ((uint64_t)data[2] << 8);
        *out_used = 3u;
        return 0;
    case 0x0Cu:
        if (length < 5u) {
            return -1;
        }
        *out_value = (uint64_t)read_u32_le(data + 1u);
        *out_used = 5u;
        return 0;
    case 0x0Eu:
        if (length < 9u) {
            return -1;
        }
        *out_value = read_u64_le(data + 1u);
        *out_used = 9u;
        return 0;
    default:
        return -1;
    }
}

static int acpi_parse_s5_types(const uint8_t* aml, uint32_t length, uint16_t* out_typ_a, uint16_t* out_typ_b) {
    if (!aml || !out_typ_a || !out_typ_b || length < 8u) {
        return -1;
    }

    for (uint32_t i = 1u; i + 5u < length; i++) {
        uint32_t pos;
        uint32_t pkg_used;
        uint64_t val_a;
        uint64_t val_b;
        uint32_t used;

        if (aml[i + 0u] != '_' || aml[i + 1u] != 'S' || aml[i + 2u] != '5' || aml[i + 3u] != '_') {
            continue;
        }

        if (aml[i - 1u] != 0x08u) {
            if (i < 2u || aml[i - 2u] != 0x08u || aml[i - 1u] != '\\') {
                continue;
            }
        }

        pos = i + 4u;
        if (pos >= length || aml[pos] != 0x12u) {
            continue;
        }
        pos++;
        if (aml_parse_pkg_length(aml + pos, length - pos, NULL, &pkg_used) != 0) {
            continue;
        }
        pos += pkg_used;
        if (pos >= length) {
            continue;
        }

        /* NumElements. */
        pos++;
        if (pos >= length) {
            continue;
        }

        if (aml_parse_integer(aml + pos, length - pos, &val_a, &used) != 0) {
            continue;
        }
        pos += used;
        if (pos >= length) {
            continue;
        }
        if (aml_parse_integer(aml + pos, length - pos, &val_b, &used) != 0) {
            continue;
        }

        *out_typ_a = (uint16_t)((val_a & 0x7u) << 10);
        *out_typ_b = (uint16_t)((val_b & 0x7u) << 10);
        return 0;
    }

    return -1;
}

static uint16_t acpi_fadt_read_cnt_port(const uint8_t* fadt, uint32_t fadt_length, uint32_t offset_legacy, uint32_t offset_x) {
    uint32_t legacy = 0u;

    if (offset_legacy + 4u <= fadt_length) {
        legacy = read_u32_le(fadt + offset_legacy);
    }
    if (legacy != 0u && legacy <= 0xFFFFu) {
        return (uint16_t)legacy;
    }

    if (offset_x + sizeof(acpi_gas_t) <= fadt_length) {
        const acpi_gas_t* gas = (const acpi_gas_t*)(const void*)(fadt + offset_x);
        if (gas->address_space_id == ACPI_SPACE_SYSTEM_IO && gas->address != 0u && gas->address <= 0xFFFFu) {
            return (uint16_t)gas->address;
        }
    }

    return 0u;
}

static int acpi_shutdown(boot_info_t* boot) {
    const acpi_sdt_header_t* fadt_header = acpi_find_table(boot, ACPI_SIG_FADT);
    const uint8_t* fadt = (const uint8_t*)fadt_header;
    uint32_t fadt_length;
    uint64_t dsdt_addr = 0u;
    const acpi_sdt_header_t* dsdt_header;
    const uint8_t* aml;
    uint32_t aml_length;
    uint16_t slp_typ_a;
    uint16_t slp_typ_b;
    uint16_t pm1a_cnt;
    uint16_t pm1b_cnt;
    uint8_t pm1_cnt_len = 0u;

    if (!fadt_header) {
        return -1;
    }

    fadt_length = fadt_header->length;
    if (fadt_length < ACPI_FADT_OFF_PM1_CNT_LEN + 1u) {
        return -1;
    }

    if (ACPI_FADT_OFF_X_DSDT + 8u <= fadt_length) {
        dsdt_addr = read_u64_le(fadt + ACPI_FADT_OFF_X_DSDT);
    }
    if (dsdt_addr == 0u && ACPI_FADT_OFF_DSDT + 4u <= fadt_length) {
        dsdt_addr = (uint64_t)read_u32_le(fadt + ACPI_FADT_OFF_DSDT);
    }
    if (dsdt_addr == 0u) {
        return -1;
    }

    pm1a_cnt = acpi_fadt_read_cnt_port(
        fadt,
        fadt_length,
        ACPI_FADT_OFF_PM1A_CNT_BLK,
        ACPI_FADT_OFF_X_PM1A_CNT_BLK
    );
    pm1b_cnt = acpi_fadt_read_cnt_port(
        fadt,
        fadt_length,
        ACPI_FADT_OFF_PM1B_CNT_BLK,
        ACPI_FADT_OFF_X_PM1B_CNT_BLK
    );
    if (pm1a_cnt == 0u) {
        return -1;
    }

    pm1_cnt_len = fadt[ACPI_FADT_OFF_PM1_CNT_LEN];
    if (pm1_cnt_len < 2u) {
        return -1;
    }

    dsdt_header = (const acpi_sdt_header_t*)(uintptr_t)dsdt_addr;
    if (!acpi_sdt_valid(dsdt_header, ACPI_SIG_DSDT)) {
        return -1;
    }
    if (dsdt_header->length <= sizeof(acpi_sdt_header_t)) {
        return -1;
    }

    aml = (const uint8_t*)dsdt_header + sizeof(acpi_sdt_header_t);
    aml_length = dsdt_header->length - (uint32_t)sizeof(acpi_sdt_header_t);
    if (acpi_parse_s5_types(aml, aml_length, &slp_typ_a, &slp_typ_b) != 0) {
        return -1;
    }

    port_out16(pm1a_cnt, (uint16_t)(slp_typ_a | ACPI_SLP_EN));
    if (pm1b_cnt != 0u) {
        port_out16(pm1b_cnt, (uint16_t)(slp_typ_b | ACPI_SLP_EN));
    }

    return 0;
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
    /* EfiResetShutdown. Required for real hardware where VM ACPI ports do nothing. */
    firmware_reset(boot, EFI_RESET_SHUTDOWN);
    (void)acpi_shutdown(boot);

    /* QEMU/Bochs/VirtualBox fallbacks. */
    port_out16(0x604u, 0x2000u);
    port_out16(0xB004u, 0x2000u);
    port_out16(0x4004u, 0x3400u);

    /* APM fallback used by some old BIOS/firmware combinations. */
    port_out16(0xB004u, 0x0000u);
    port_out16(0xB004u, 0x2000u);
}

void power_reboot(boot_info_t* boot) {
    firmware_reset(boot, 0u);

    while (port_in8(0x64u) & 0x02u) {
    }
    port_out8(0x64u, 0xFEu);
}

int power_set_gop_mode_pref(boot_info_t* boot, uint8_t has_mode, uint32_t mode) {
    const uint32_t attrs = 0x00000001u | 0x00000002u | 0x00000004u;
    uint32_t value = has_mode ? mode : 0xFFFFFFFFu;
    efi_set_variable_t set_variable;
    uint64_t status;

    if (!boot || boot->efi_set_variable == 0u) {
        return -1;
    }

    set_variable = (efi_set_variable_t)(uintptr_t)boot->efi_set_variable;
    status = set_variable(
        g_gop_pref_var_name,
        &g_gop_pref_var_guid,
        attrs,
        sizeof(value),
        &value
    );
    return status == 0u ? 0 : -1;
}

int power_get_gop_mode_pref(boot_info_t* boot, uint8_t* out_has_mode, uint32_t* out_mode) {
    uint32_t attrs = 0u;
    uint64_t size = sizeof(uint32_t);
    uint32_t value = 0u;
    efi_get_variable_t get_variable;
    uint64_t status;

    if (!boot || !out_has_mode || !out_mode || boot->efi_get_variable == 0u) {
        return -1;
    }

    get_variable = (efi_get_variable_t)(uintptr_t)boot->efi_get_variable;
    status = get_variable(
        g_gop_pref_var_name,
        &g_gop_pref_var_guid,
        &attrs,
        &size,
        &value
    );
    if (status != 0u || size != sizeof(uint32_t)) {
        return -1;
    }

    if (value == 0xFFFFFFFFu) {
        *out_has_mode = 0u;
        *out_mode = 0u;
    } else {
        *out_has_mode = 1u;
        *out_mode = value;
    }
    return 0;
}
