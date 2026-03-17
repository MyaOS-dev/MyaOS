#ifndef PCI_H
#define PCI_H

#include <stdint.h>

int pci_find_device(uint16_t vendor_id, uint16_t device_id, uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

#endif
