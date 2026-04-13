#include "pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

#define PCI_VENDOR_ID_OFFSET 0x00u
#define PCI_DEVICE_ID_OFFSET 0x02u
#define PCI_COMMAND_OFFSET 0x04u
#define PCI_STATUS_OFFSET 0x06u
#define PCI_REVISION_OFFSET 0x08u
#define PCI_PROGIF_OFFSET 0x09u
#define PCI_SUBCLASS_OFFSET 0x0Au
#define PCI_CLASS_OFFSET 0x0Bu
#define PCI_HEADER_TYPE_OFFSET 0x0Eu
#define PCI_CAP_PTR_OFFSET 0x34u
#define PCI_CAP_PTR_CARDBUS_OFFSET 0x14u
#define PCI_IRQ_LINE_OFFSET 0x3Cu
#define PCI_IRQ_PIN_OFFSET 0x3Du
#define PCI_SUBSYSTEM_VENDOR_OFFSET 0x2Cu
#define PCI_SUBSYSTEM_DEVICE_OFFSET 0x2Eu

#define PCI_HEADER_TYPE_MULTIFUNC 0x80u
#define PCI_HEADER_TYPE_MASK 0x7Fu
#define PCI_HEADER_TYPE_DEVICE 0x00u
#define PCI_HEADER_TYPE_BRIDGE 0x01u
#define PCI_HEADER_TYPE_CARDBUS 0x02u

#define PCI_STATUS_CAP_LIST 0x0010u

#define PCI_CAP_ID_MSI 0x05u
#define PCI_CAP_ID_MSIX 0x11u

#define PCI_MSI_CTRL_ENABLE 0x0001u
#define PCI_MSI_CTRL_MMC_MASK 0x000Eu
#define PCI_MSI_CTRL_MME_MASK 0x0070u
#define PCI_MSI_CTRL_64BIT 0x0080u
#define PCI_MSI_CTRL_PER_VECTOR_MASKING 0x0100u

#define PCI_MSIX_CTRL_ENABLE 0x8000u
#define PCI_MSIX_CTRL_FUNC_MASK 0x4000u
#define PCI_MSIX_TABLE_BIR_MASK 0x00000007u
#define PCI_MSIX_TABLE_OFFSET_MASK 0xFFFFFFF8u

#define PCI_COMMAND_INTX_DISABLE 0x0400u

static inline void io_out32(uint16_t port, uint32_t value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t io_in32(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return 0x80000000u |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) |
           ((uint32_t)offset & 0xFCu);
}

static uint8_t pci_cap_ptr_offset_for_header(uint8_t header_type) {
    if ((header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_CARDBUS) {
        return PCI_CAP_PTR_CARDBUS_OFFSET;
    }
    return PCI_CAP_PTR_OFFSET;
}

static uint8_t pci_function_exists(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_config_read16(bus, slot, func, PCI_VENDOR_ID_OFFSET) != 0xFFFFu;
}

static uint8_t pci_function_count_for_slot(uint8_t bus, uint8_t slot) {
    uint8_t header_type;

    if (!pci_function_exists(bus, slot, 0u)) {
        return 0u;
    }

    header_type = pci_config_read8(bus, slot, 0u, PCI_HEADER_TYPE_OFFSET);
    return (header_type & PCI_HEADER_TYPE_MULTIFUNC) ? 8u : 1u;
}

static uint8_t pci_bar_count_for_header(uint8_t header_type) {
    switch (header_type & PCI_HEADER_TYPE_MASK) {
        case PCI_HEADER_TYPE_DEVICE:
            return 6u;
        case PCI_HEADER_TYPE_BRIDGE:
            return 2u;
        default:
            return 0u;
    }
}

static void pci_decode_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index, pci_bar_info_t* out_bar) {
    uint8_t offset;
    uint32_t low;

    if (!out_bar || bar_index >= 6u) {
        return;
    }

    out_bar->present = 0u;
    out_bar->is_io = 0u;
    out_bar->is_64 = 0u;
    out_bar->prefetchable = 0u;
    out_bar->raw_low = 0u;
    out_bar->raw_high = 0u;
    out_bar->base = 0u;

    offset = (uint8_t)(0x10u + bar_index * 4u);
    low = pci_config_read32(bus, slot, func, offset);
    out_bar->raw_low = low;

    if (low == 0u) {
        return;
    }

    out_bar->present = 1u;

    if ((low & 0x1u) != 0u) {
        out_bar->is_io = 1u;
        out_bar->base = (uint64_t)(low & 0xFFFFFFFCu);
        return;
    }

    out_bar->is_io = 0u;
    out_bar->prefetchable = (uint8_t)((low >> 3) & 0x1u);

    if (((low >> 1) & 0x3u) == 0x2u && bar_index < 5u) {
        uint32_t high = pci_config_read32(bus, slot, func, (uint8_t)(offset + 4u));
        out_bar->is_64 = 1u;
        out_bar->raw_high = high;
        out_bar->base = ((uint64_t)high << 32) | (uint64_t)(low & 0xFFFFFFF0u);
    } else {
        out_bar->is_64 = 0u;
        out_bar->base = (uint64_t)(low & 0xFFFFFFF0u);
    }
}

static int pci_get_mem_bar_base(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index, uint64_t* out_base) {
    uint8_t offset;
    uint32_t low;
    uint64_t base;

    if (!out_base || bar_index >= 6u) {
        return -1;
    }

    offset = (uint8_t)(0x10u + bar_index * 4u);
    low = pci_config_read32(bus, slot, func, offset);
    if (low == 0u || (low & 0x1u) != 0u) {
        return -1;
    }

    base = (uint64_t)(low & 0xFFFFFFF0u);
    if (((low >> 1) & 0x3u) == 0x2u) {
        uint32_t high;
        if (bar_index >= 5u) {
            return -1;
        }
        high = pci_config_read32(bus, slot, func, (uint8_t)(offset + 4u));
        base |= ((uint64_t)high << 32);
    }

    if (base == 0u) {
        return -1;
    }

    *out_base = base;
    return 0;
}

static void pci_disable_legacy_intx(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_config_read16(bus, slot, func, PCI_COMMAND_OFFSET);
    cmd |= PCI_COMMAND_INTX_DISABLE;
    pci_config_write16(bus, slot, func, PCI_COMMAND_OFFSET, cmd);
}

static void pci_disable_msi_if_present(uint8_t bus, uint8_t slot, uint8_t func) {
    uint8_t cap;

    if (pci_find_capability(bus, slot, func, PCI_CAP_ID_MSI, &cap) != 0) {
        return;
    }

    {
        uint16_t ctrl = pci_config_read16(bus, slot, func, (uint8_t)(cap + 0x02u));
        ctrl &= (uint16_t)~PCI_MSI_CTRL_ENABLE;
        pci_config_write16(bus, slot, func, (uint8_t)(cap + 0x02u), ctrl);
    }
}

static void pci_disable_msix_if_present(uint8_t bus, uint8_t slot, uint8_t func) {
    uint8_t cap;

    if (pci_find_capability(bus, slot, func, PCI_CAP_ID_MSIX, &cap) != 0) {
        return;
    }

    {
        uint16_t ctrl = pci_config_read16(bus, slot, func, (uint8_t)(cap + 0x02u));
        ctrl &= (uint16_t)~PCI_MSIX_CTRL_ENABLE;
        pci_config_write16(bus, slot, func, (uint8_t)(cap + 0x02u), ctrl);
    }
}

static int pci_enable_msix_internal(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t vector,
    uint8_t dest_apic_id,
    uint8_t* out_mode
) {
    uint8_t cap;
    uint16_t ctrl;
    uint16_t table_size;
    uint32_t table_info;
    uint8_t bir;
    uint32_t table_offset;
    uint64_t table_base;
    volatile uint32_t* entry;

    if (pci_find_capability(bus, slot, func, PCI_CAP_ID_MSIX, &cap) != 0) {
        return -1;
    }

    ctrl = pci_config_read16(bus, slot, func, (uint8_t)(cap + 0x02u));
    table_size = (uint16_t)((ctrl & 0x07FFu) + 1u);
    if (table_size == 0u) {
        return -1;
    }

    table_info = pci_config_read32(bus, slot, func, (uint8_t)(cap + 0x04u));
    bir = (uint8_t)(table_info & PCI_MSIX_TABLE_BIR_MASK);
    table_offset = table_info & PCI_MSIX_TABLE_OFFSET_MASK;

    if (pci_get_mem_bar_base(bus, slot, func, bir, &table_base) != 0) {
        return -1;
    }

    table_base += (uint64_t)table_offset;
    entry = (volatile uint32_t*)(uintptr_t)table_base;

    pci_disable_msi_if_present(bus, slot, func);

    ctrl = (uint16_t)((ctrl | PCI_MSIX_CTRL_FUNC_MASK) & (uint16_t)~PCI_MSIX_CTRL_ENABLE);
    pci_config_write16(bus, slot, func, (uint8_t)(cap + 0x02u), ctrl);

    entry[3] = 0x1u;
    entry[0] = 0xFEE00000u | ((uint32_t)dest_apic_id << 12);
    entry[1] = 0u;
    entry[2] = (uint32_t)vector;
    entry[3] = 0u;

    ctrl = pci_config_read16(bus, slot, func, (uint8_t)(cap + 0x02u));
    ctrl |= PCI_MSIX_CTRL_ENABLE;
    ctrl &= (uint16_t)~PCI_MSIX_CTRL_FUNC_MASK;
    pci_config_write16(bus, slot, func, (uint8_t)(cap + 0x02u), ctrl);

    pci_disable_legacy_intx(bus, slot, func);
    if (out_mode) {
        *out_mode = PCI_IRQ_MODE_MSIX;
    }
    return 0;
}

static int pci_enable_msi_internal(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t vector,
    uint8_t dest_apic_id,
    uint8_t* out_mode
) {
    uint8_t cap;
    uint16_t ctrl;
    uint8_t mmc_power;
    uint8_t mme_power;
    uint8_t is_64;
    uint8_t has_mask_bits;
    uint8_t data_off;
    uint32_t addr_low;

    if (pci_find_capability(bus, slot, func, PCI_CAP_ID_MSI, &cap) != 0) {
        return -1;
    }

    pci_disable_msix_if_present(bus, slot, func);

    ctrl = pci_config_read16(bus, slot, func, (uint8_t)(cap + 0x02u));
    mmc_power = (uint8_t)((ctrl & PCI_MSI_CTRL_MMC_MASK) >> 1);
    mme_power = 0u;
    if (mme_power > mmc_power) {
        mme_power = mmc_power;
    }
    is_64 = (ctrl & PCI_MSI_CTRL_64BIT) ? 1u : 0u;
    has_mask_bits = (ctrl & PCI_MSI_CTRL_PER_VECTOR_MASKING) ? 1u : 0u;

    ctrl &= (uint16_t)~PCI_MSI_CTRL_ENABLE;
    ctrl &= (uint16_t)~PCI_MSI_CTRL_MME_MASK;
    ctrl |= (uint16_t)((uint16_t)mme_power << 4);
    pci_config_write16(bus, slot, func, (uint8_t)(cap + 0x02u), ctrl);

    addr_low = 0xFEE00000u | ((uint32_t)dest_apic_id << 12);
    pci_config_write32(bus, slot, func, (uint8_t)(cap + 0x04u), addr_low);
    if (is_64) {
        pci_config_write32(bus, slot, func, (uint8_t)(cap + 0x08u), 0u);
    }

    data_off = (uint8_t)(cap + (is_64 ? 0x0Cu : 0x08u));
    pci_config_write16(bus, slot, func, data_off, (uint16_t)vector);

    if (has_mask_bits) {
        uint8_t mask_off = (uint8_t)(cap + (is_64 ? 0x10u : 0x0Cu));
        pci_config_write32(bus, slot, func, mask_off, 0u);
    }

    ctrl = pci_config_read16(bus, slot, func, (uint8_t)(cap + 0x02u));
    ctrl |= PCI_MSI_CTRL_ENABLE;
    pci_config_write16(bus, slot, func, (uint8_t)(cap + 0x02u), ctrl);

    pci_disable_legacy_intx(bus, slot, func);
    if (out_mode) {
        *out_mode = PCI_IRQ_MODE_MSI;
    }
    return 0;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    io_out32(PCI_CONFIG_ADDRESS, pci_address(bus, slot, func, offset));
    return io_in32(PCI_CONFIG_DATA);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, func, (uint8_t)(offset & 0xFCu));
    uint32_t shift = (uint32_t)(offset & 0x3u) * 8u;
    return (uint8_t)((value >> shift) & 0xFFu);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, slot, func, (uint8_t)(offset & 0xFCu));
    uint32_t shift = (uint32_t)(offset & 0x2u) * 8u;
    return (uint16_t)((value >> shift) & 0xFFFFu);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    io_out32(PCI_CONFIG_ADDRESS, pci_address(bus, slot, func, offset));
    io_out32(PCI_CONFIG_DATA, value);
}

void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    uint8_t aligned = (uint8_t)(offset & 0xFCu);
    uint32_t current = pci_config_read32(bus, slot, func, aligned);
    uint32_t shift = (uint32_t)(offset & 0x3u) * 8u;
    uint32_t mask = 0xFFu << shift;
    uint32_t next = (current & ~mask) | ((uint32_t)value << shift);

    pci_config_write32(bus, slot, func, aligned, next);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint8_t aligned = (uint8_t)(offset & 0xFCu);
    uint32_t current = pci_config_read32(bus, slot, func, aligned);
    uint32_t shift = (uint32_t)(offset & 0x2u) * 8u;
    uint32_t mask = 0xFFFFu << shift;
    uint32_t next = (current & ~mask) | ((uint32_t)value << shift);

    pci_config_write32(bus, slot, func, aligned, next);
}

int pci_get_device_info(uint8_t bus, uint8_t slot, uint8_t func, pci_device_info_t* out_info) {
    uint8_t header;
    uint8_t bar_count;

    if (!out_info || !pci_function_exists(bus, slot, func)) {
        return -1;
    }

    for (uint32_t i = 0; i < (uint32_t)sizeof(*out_info); i++) {
        ((uint8_t*)out_info)[i] = 0u;
    }

    out_info->bus = bus;
    out_info->slot = slot;
    out_info->func = func;
    out_info->vendor_id = pci_config_read16(bus, slot, func, PCI_VENDOR_ID_OFFSET);
    out_info->device_id = pci_config_read16(bus, slot, func, PCI_DEVICE_ID_OFFSET);
    out_info->command = pci_config_read16(bus, slot, func, PCI_COMMAND_OFFSET);
    out_info->status = pci_config_read16(bus, slot, func, PCI_STATUS_OFFSET);
    out_info->revision = pci_config_read8(bus, slot, func, PCI_REVISION_OFFSET);
    out_info->prog_if = pci_config_read8(bus, slot, func, PCI_PROGIF_OFFSET);
    out_info->subclass = pci_config_read8(bus, slot, func, PCI_SUBCLASS_OFFSET);
    out_info->class_code = pci_config_read8(bus, slot, func, PCI_CLASS_OFFSET);
    out_info->header_type = pci_config_read8(bus, slot, func, PCI_HEADER_TYPE_OFFSET);
    out_info->irq_line = pci_config_read8(bus, slot, func, PCI_IRQ_LINE_OFFSET);
    out_info->irq_pin = pci_config_read8(bus, slot, func, PCI_IRQ_PIN_OFFSET);

    header = (uint8_t)(out_info->header_type & PCI_HEADER_TYPE_MASK);
    if (header == PCI_HEADER_TYPE_DEVICE) {
        out_info->subsystem_vendor_id = pci_config_read16(bus, slot, func, PCI_SUBSYSTEM_VENDOR_OFFSET);
        out_info->subsystem_device_id = pci_config_read16(bus, slot, func, PCI_SUBSYSTEM_DEVICE_OFFSET);
    }

    bar_count = pci_bar_count_for_header(out_info->header_type);
    out_info->bar_count = bar_count;
    for (uint8_t i = 0; i < bar_count; i++) {
        pci_decode_bar(bus, slot, func, i, &out_info->bars[i]);
        if (out_info->bars[i].is_64 && i + 1u < bar_count) {
            i++;
        }
    }

    out_info->has_cap_list = (out_info->status & PCI_STATUS_CAP_LIST) ? 1u : 0u;
    if (out_info->has_cap_list) {
        out_info->has_msi = (pci_find_capability(bus, slot, func, PCI_CAP_ID_MSI, 0) == 0) ? 1u : 0u;
        out_info->has_msix = (pci_find_capability(bus, slot, func, PCI_CAP_ID_MSIX, 0) == 0) ? 1u : 0u;
    }

    return 0;
}

int pci_scan_devices(pci_device_info_t* out_entries, uint32_t max_entries, uint32_t* out_count) {
    uint32_t written = 0u;
    uint8_t truncated = 0u;

    if (!out_count || (max_entries != 0u && !out_entries)) {
        return -1;
    }

    for (uint32_t bus = 0; bus < 256u; bus++) {
        for (uint32_t slot = 0; slot < 32u; slot++) {
            uint8_t funcs = pci_function_count_for_slot((uint8_t)bus, (uint8_t)slot);
            if (funcs == 0u) {
                continue;
            }

            for (uint32_t func = 0; func < funcs; func++) {
                if (!pci_function_exists((uint8_t)bus, (uint8_t)slot, (uint8_t)func)) {
                    continue;
                }

                if (written < max_entries) {
                    if (pci_get_device_info((uint8_t)bus, (uint8_t)slot, (uint8_t)func, &out_entries[written]) == 0) {
                        written++;
                    }
                } else {
                    truncated = 1u;
                }
            }
        }
    }

    *out_count = written;
    return truncated ? 1 : 0;
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func) {
    for (uint32_t bus = 0; bus < 256u; bus++) {
        for (uint32_t slot = 0; slot < 32u; slot++) {
            uint8_t funcs = pci_function_count_for_slot((uint8_t)bus, (uint8_t)slot);
            if (funcs == 0u) {
                continue;
            }

            for (uint32_t func = 0; func < funcs; func++) {
                uint16_t ven;
                uint16_t dev;

                if (!pci_function_exists((uint8_t)bus, (uint8_t)slot, (uint8_t)func)) {
                    continue;
                }

                ven = pci_config_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func, PCI_VENDOR_ID_OFFSET);
                dev = pci_config_read16((uint8_t)bus, (uint8_t)slot, (uint8_t)func, PCI_DEVICE_ID_OFFSET);

                if (ven == vendor_id && dev == device_id) {
                    if (out_bus) {
                        *out_bus = (uint8_t)bus;
                    }
                    if (out_slot) {
                        *out_slot = (uint8_t)slot;
                    }
                    if (out_func) {
                        *out_func = (uint8_t)func;
                    }
                    return 0;
                }
            }
        }
    }

    return -1;
}

int pci_find_capability(uint8_t bus, uint8_t slot, uint8_t func, uint8_t cap_id, uint8_t* out_offset) {
    uint16_t status;
    uint8_t header_type;
    uint8_t cap_ptr_off;
    uint8_t cap_ptr;

    if (!pci_function_exists(bus, slot, func)) {
        return -1;
    }

    status = pci_config_read16(bus, slot, func, PCI_STATUS_OFFSET);
    if ((status & PCI_STATUS_CAP_LIST) == 0u) {
        return -1;
    }

    header_type = pci_config_read8(bus, slot, func, PCI_HEADER_TYPE_OFFSET);
    cap_ptr_off = pci_cap_ptr_offset_for_header(header_type);
    cap_ptr = (uint8_t)(pci_config_read8(bus, slot, func, cap_ptr_off) & 0xFCu);

    for (uint32_t hop = 0; hop < 96u; hop++) {
        uint8_t cap;
        uint8_t next;

        if (cap_ptr < 0x40u) {
            break;
        }

        cap = pci_config_read8(bus, slot, func, cap_ptr);
        next = pci_config_read8(bus, slot, func, (uint8_t)(cap_ptr + 1u));

        if (cap == cap_id) {
            if (out_offset) {
                *out_offset = cap_ptr;
            }
            return 0;
        }

        next = (uint8_t)(next & 0xFCu);
        if (next == 0u || next == cap_ptr) {
            break;
        }
        cap_ptr = next;
    }

    return -1;
}

int pci_enable_message_signaled_irq(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t vector,
    uint8_t dest_apic_id,
    uint8_t* out_mode
) {
    if (out_mode) {
        *out_mode = 0u;
    }
    if (vector < 32u || vector == 0x80u) {
        return -1;
    }

    if (pci_enable_msix_internal(bus, slot, func, vector, dest_apic_id, out_mode) == 0) {
        return 0;
    }

    return pci_enable_msi_internal(bus, slot, func, vector, dest_apic_id, out_mode);
}

int pci_enable_msi(uint8_t bus, uint8_t slot, uint8_t func, uint8_t vector, uint8_t dest_apic_id) {
    return pci_enable_message_signaled_irq(bus, slot, func, vector, dest_apic_id, 0);
}
