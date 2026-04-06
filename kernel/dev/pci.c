#include "pci.h"

#define PCI_CONFIG_ADDRESS 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

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

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    io_out32(PCI_CONFIG_ADDRESS, pci_address(bus, slot, func, offset));
    return io_in32(PCI_CONFIG_DATA);
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

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint8_t aligned = (uint8_t)(offset & 0xFCu);
    uint32_t current = pci_config_read32(bus, slot, func, aligned);
    uint32_t shift = (uint32_t)(offset & 0x2u) * 8u;
    uint32_t mask = 0xFFFFu << shift;
    uint32_t next = (current & ~mask) | ((uint32_t)value << shift);

    pci_config_write32(bus, slot, func, aligned, next);
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func) {
    for (uint32_t bus = 0; bus < 256u; bus++) {
        for (uint32_t slot = 0; slot < 32u; slot++) {
            for (uint32_t func = 0; func < 8u; func++) {
                uint32_t id = pci_config_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x00u);
                uint16_t ven = (uint16_t)(id & 0xFFFFu);
                uint16_t dev = (uint16_t)((id >> 16) & 0xFFFFu);

                if (ven == 0xFFFFu) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }
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
