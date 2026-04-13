#ifndef APIC_X86_64_H
#define APIC_X86_64_H

#include <stdint.h>

int apic_init(void);
int apic_is_enabled(void);
uint8_t apic_local_id(void);
void apic_send_eoi(void);
int apic_timer_start(uint32_t hz, uint8_t vector, uint32_t* out_actual_hz);
int ioapic_is_present(void);
int ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t masked);

#endif
