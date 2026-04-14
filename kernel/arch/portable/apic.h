#ifndef APIC_PORTABLE_H
#define APIC_PORTABLE_H

#include <stdint.h>

static inline int apic_init(void) { return -1; }
static inline int apic_is_enabled(void) { return 0; }
static inline uint8_t apic_local_id(void) { return 0u; }
static inline void apic_send_eoi(void) {}
static inline int apic_timer_start(uint32_t hz, uint8_t vector, uint32_t* out_actual_hz) {
    (void)hz;
    (void)vector;
    if (out_actual_hz) {
        *out_actual_hz = 0u;
    }
    return -1;
}
static inline int ioapic_is_present(void) { return 0; }
static inline int ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t masked) {
    (void)irq;
    (void)vector;
    (void)masked;
    return -1;
}

#endif
