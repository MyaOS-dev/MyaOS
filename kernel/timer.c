#include "timer.h"
#include <stdint.h>

#define PIT_BASE_FREQUENCY 1193182u

static volatile uint64_t g_timer_ticks;
static uint32_t g_timer_hz = 100u;

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

void timer_init(uint32_t hz) {
    if (hz == 0) {
        hz = 100u;
    }
    if (hz > 1000u) {
        hz = 1000u;
    }

    uint32_t divisor = PIT_BASE_FREQUENCY / hz;
    if (divisor == 0) {
        divisor = 1;
    }
    if (divisor > 0xFFFFu) {
        divisor = 0xFFFFu;
    }

    g_timer_hz = PIT_BASE_FREQUENCY / divisor;
    if (g_timer_hz == 0) {
        g_timer_hz = 1;
    }

    out8(0x43u, 0x36u);
    out8(0x40u, (uint8_t)(divisor & 0xFFu));
    out8(0x40u, (uint8_t)((divisor >> 8) & 0xFFu));
}

void timer_irq_tick(void) {
    g_timer_ticks++;
}

uint64_t timer_ticks(void) {
    return g_timer_ticks;
}

uint32_t timer_hz(void) {
    return g_timer_hz;
}
