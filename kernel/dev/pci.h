#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_IRQ_MODE_MSI 1u
#define PCI_IRQ_MODE_MSIX 2u

typedef struct {
    uint8_t present;
    uint8_t is_io;
    uint8_t is_64;
    uint8_t prefetchable;
    uint32_t raw_low;
    uint32_t raw_high;
    uint64_t base;
} pci_bar_info_t;

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t header_type;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t has_cap_list;
    uint8_t has_msi;
    uint8_t has_msix;
    uint8_t bar_count;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;
    uint16_t command;
    uint16_t status;
    pci_bar_info_t bars[6];
} pci_device_info_t;

int pci_find_device(uint16_t vendor_id, uint16_t device_id, uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func);
int pci_get_device_info(uint8_t bus, uint8_t slot, uint8_t func, pci_device_info_t* out_info);
int pci_scan_devices(pci_device_info_t* out_entries, uint32_t max_entries, uint32_t* out_count);
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
int pci_find_capability(uint8_t bus, uint8_t slot, uint8_t func, uint8_t cap_id, uint8_t* out_offset);
int pci_enable_msi(uint8_t bus, uint8_t slot, uint8_t func, uint8_t vector, uint8_t dest_apic_id);
int pci_enable_message_signaled_irq(
    uint8_t bus,
    uint8_t slot,
    uint8_t func,
    uint8_t vector,
    uint8_t dest_apic_id,
    uint8_t* out_mode
);

#endif
