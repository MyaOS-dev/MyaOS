#include "apic.h"
#include <stddef.h>
#include <stdint.h>

#define IA32_APIC_BASE_MSR 0x1Bu
#define IA32_APIC_BASE_ENABLE (1ull << 11)
#define IA32_APIC_BASE_ADDR_MASK 0x000FFFFFFFFFF000ull
#define APIC_DEFAULT_PHYS_BASE 0xFEE00000u
#define IOAPIC_DEFAULT_PHYS_BASE 0xFEC00000u

#define APIC_REG_ID 0x020u
#define APIC_REG_EOI 0x0B0u
#define APIC_REG_SVR 0x0F0u
#define APIC_REG_ESR 0x280u
#define APIC_REG_LVT_TIMER 0x320u
#define APIC_REG_LVT_LINT0 0x350u
#define APIC_REG_LVT_LINT1 0x360u
#define APIC_REG_LVT_ERROR 0x370u
#define APIC_REG_TIMER_INITCNT 0x380u
#define APIC_REG_TIMER_CURRCNT 0x390u
#define APIC_REG_TIMER_DIV 0x3E0u

#define APIC_SVR_ENABLE 0x00000100u
#define APIC_LVT_MASKED 0x00010000u
#define APIC_LVT_TIMER_PERIODIC 0x00020000u

#define IOAPIC_REG_ID 0x00u
#define IOAPIC_REG_VER 0x01u
#define IOAPIC_REDIR_BASE 0x10u

#define PIT_BASE_FREQUENCY 1193182u

static volatile uint32_t* g_lapic_mmio;
static volatile uint32_t* g_ioapic_mmio;
static uint32_t g_ioapic_redir_count;
static uint32_t g_lapic_ticks_per_sec;
static uint8_t g_apic_enabled;
static uint8_t g_ioapic_present;
static uint8_t g_local_apic_id;

static inline void io_out8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t io_in8(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(leaf), "c"(subleaf));

    if (eax) {
        *eax = a;
    }
    if (ebx) {
        *ebx = b;
    }
    if (ecx) {
        *ecx = c;
    }
    if (edx) {
        *edx = d;
    }
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFu);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static inline uint32_t lapic_read(uint32_t reg) {
    volatile uint32_t* ptr;

    if (!g_lapic_mmio) {
        return 0u;
    }

    ptr = (volatile uint32_t*)(uintptr_t)((uintptr_t)g_lapic_mmio + reg);
    return *ptr;
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
    volatile uint32_t* ptr;

    if (!g_lapic_mmio) {
        return;
    }

    ptr = (volatile uint32_t*)(uintptr_t)((uintptr_t)g_lapic_mmio + reg);
    *ptr = value;
    (void)lapic_read(APIC_REG_ID);
}

static inline uint32_t ioapic_read(uint32_t reg) {
    volatile uint32_t* sel;
    volatile uint32_t* data;

    if (!g_ioapic_mmio) {
        return 0xFFFFFFFFu;
    }

    sel = (volatile uint32_t*)(uintptr_t)((uintptr_t)g_ioapic_mmio + 0x00u);
    data = (volatile uint32_t*)(uintptr_t)((uintptr_t)g_ioapic_mmio + 0x10u);
    *sel = reg;
    return *data;
}

static inline void ioapic_write(uint32_t reg, uint32_t value) {
    volatile uint32_t* sel;
    volatile uint32_t* data;

    if (!g_ioapic_mmio) {
        return;
    }

    sel = (volatile uint32_t*)(uintptr_t)((uintptr_t)g_ioapic_mmio + 0x00u);
    data = (volatile uint32_t*)(uintptr_t)((uintptr_t)g_ioapic_mmio + 0x10u);
    *sel = reg;
    *data = value;
}

static int apic_cpu_supported(void) {
    uint32_t edx = 0;

    cpuid(1u, 0u, NULL, NULL, NULL, &edx);
    return (edx & (1u << 9)) ? 1 : 0;
}

static uint32_t ioapic_sanitize_redir_count(uint32_t count) {
    if (count == 0u || count > 120u) {
        return 0u;
    }
    return count;
}

static int lapic_calibrate_with_pit(uint32_t* out_ticks_per_sec) {
    uint16_t pit_reload;
    uint32_t elapsed;
    uint32_t timeout;
    uint8_t port61_before;

    if (!out_ticks_per_sec) {
        return -1;
    }

    port61_before = io_in8(0x61u);

    io_out8(0x61u, (uint8_t)((port61_before & (uint8_t)~0x02u) | 0x01u));
    io_out8(0x43u, 0xB0u);

    pit_reload = (uint16_t)(PIT_BASE_FREQUENCY / 100u);
    if (pit_reload == 0u) {
        pit_reload = 1u;
    }
    io_out8(0x42u, (uint8_t)(pit_reload & 0xFFu));
    io_out8(0x42u, (uint8_t)((pit_reload >> 8) & 0xFFu));

    timeout = 0u;
    while ((io_in8(0x61u) & 0x20u) != 0u) {
        timeout++;
        if (timeout > 1000000u) {
            io_out8(0x61u, port61_before);
            return -1;
        }
    }

    lapic_write(APIC_REG_TIMER_DIV, 0x03u);
    lapic_write(APIC_REG_LVT_TIMER, APIC_LVT_MASKED);
    lapic_write(APIC_REG_TIMER_INITCNT, 0xFFFFFFFFu);

    timeout = 0u;
    while ((io_in8(0x61u) & 0x20u) == 0u) {
        timeout++;
        if (timeout > 10000000u) {
            io_out8(0x61u, port61_before);
            return -1;
        }
    }

    elapsed = 0xFFFFFFFFu - lapic_read(APIC_REG_TIMER_CURRCNT);
    io_out8(0x61u, port61_before);

    if (elapsed == 0u) {
        return -1;
    }

    {
        uint64_t ticks = (uint64_t)elapsed * 100ull;
        if (ticks > 0xFFFFFFFFull) {
            ticks = 0xFFFFFFFFull;
        }
        *out_ticks_per_sec = (uint32_t)ticks;
    }

    if (*out_ticks_per_sec < 1000u) {
        return -1;
    }

    return 0;
}

int apic_init(void) {
    uint64_t apic_base;
    uint64_t lapic_phys;
    uint32_t ver;
    uint32_t io_ver;
    uint32_t redir_count;

    g_lapic_mmio = NULL;
    g_ioapic_mmio = NULL;
    g_ioapic_redir_count = 0u;
    g_lapic_ticks_per_sec = 0u;
    g_apic_enabled = 0u;
    g_ioapic_present = 0u;
    g_local_apic_id = 0u;

    if (!apic_cpu_supported()) {
        return -1;
    }

    apic_base = rdmsr(IA32_APIC_BASE_MSR);
    apic_base |= IA32_APIC_BASE_ENABLE;
    wrmsr(IA32_APIC_BASE_MSR, apic_base);

    lapic_phys = apic_base & IA32_APIC_BASE_ADDR_MASK;
    if (lapic_phys == 0u) {
        lapic_phys = APIC_DEFAULT_PHYS_BASE;
    }

    g_lapic_mmio = (volatile uint32_t*)(uintptr_t)lapic_phys;
    ver = lapic_read(APIC_REG_SVR);
    if (ver == 0xFFFFFFFFu) {
        g_lapic_mmio = NULL;
        return -1;
    }

    lapic_write(APIC_REG_SVR, (ver & 0xFFFFFF00u) | APIC_SVR_ENABLE | 0xFFu);
    lapic_write(APIC_REG_LVT_LINT0, APIC_LVT_MASKED);
    lapic_write(APIC_REG_LVT_LINT1, APIC_LVT_MASKED);
    lapic_write(APIC_REG_LVT_ERROR, APIC_LVT_MASKED | 0xFEu);
    lapic_write(APIC_REG_ESR, 0u);
    lapic_write(APIC_REG_ESR, 0u);
    lapic_write(APIC_REG_EOI, 0u);

    g_local_apic_id = (uint8_t)((lapic_read(APIC_REG_ID) >> 24) & 0xFFu);
    g_apic_enabled = 1u;

    g_ioapic_mmio = (volatile uint32_t*)(uintptr_t)IOAPIC_DEFAULT_PHYS_BASE;
    io_ver = ioapic_read(IOAPIC_REG_VER);
    if (io_ver != 0xFFFFFFFFu) {
        redir_count = ((io_ver >> 16) & 0xFFu) + 1u;
        redir_count = ioapic_sanitize_redir_count(redir_count);
        if (redir_count > 0u) {
            g_ioapic_redir_count = redir_count;
            g_ioapic_present = 1u;

            for (uint32_t i = 0; i < g_ioapic_redir_count; i++) {
                uint32_t low_reg = IOAPIC_REDIR_BASE + i * 2u;
                uint32_t high_reg = low_reg + 1u;

                ioapic_write(high_reg, ((uint32_t)g_local_apic_id) << 24);
                ioapic_write(low_reg, APIC_LVT_MASKED | 0x20u);
            }
        }
    }

    (void)ioapic_read(IOAPIC_REG_ID);
    return 0;
}

int apic_is_enabled(void) {
    return g_apic_enabled ? 1 : 0;
}

uint8_t apic_local_id(void) {
    return g_local_apic_id;
}

void apic_send_eoi(void) {
    if (!g_apic_enabled) {
        return;
    }
    lapic_write(APIC_REG_EOI, 0u);
}

int apic_timer_start(uint32_t hz, uint8_t vector, uint32_t* out_actual_hz) {
    uint32_t init_count;
    uint32_t actual_hz;

    if (!g_apic_enabled) {
        return -1;
    }

    if (hz == 0u) {
        hz = 100u;
    } else if (hz > 2000u) {
        hz = 2000u;
    }

    if (g_lapic_ticks_per_sec == 0u) {
        if (lapic_calibrate_with_pit(&g_lapic_ticks_per_sec) != 0) {
            g_lapic_ticks_per_sec = 100000000u;
        }
    }

    init_count = g_lapic_ticks_per_sec / hz;
    if (init_count == 0u) {
        init_count = 1u;
    }
    actual_hz = g_lapic_ticks_per_sec / init_count;
    if (actual_hz == 0u) {
        actual_hz = hz;
    }

    lapic_write(APIC_REG_TIMER_DIV, 0x03u);
    lapic_write(APIC_REG_LVT_TIMER, APIC_LVT_TIMER_PERIODIC | (uint32_t)vector);
    lapic_write(APIC_REG_TIMER_INITCNT, init_count);

    if (out_actual_hz) {
        *out_actual_hz = actual_hz;
    }
    return 0;
}

int ioapic_is_present(void) {
    return g_ioapic_present ? 1 : 0;
}

int ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t masked) {
    uint32_t low_reg;
    uint32_t high_reg;
    uint32_t low;
    uint32_t high;

    if (!g_apic_enabled || !g_ioapic_present) {
        return -1;
    }
    if ((uint32_t)irq >= g_ioapic_redir_count) {
        return -1;
    }

    low_reg = IOAPIC_REDIR_BASE + (uint32_t)irq * 2u;
    high_reg = low_reg + 1u;
    low = (uint32_t)vector;
    if (masked) {
        low |= APIC_LVT_MASKED;
    }
    high = ((uint32_t)g_local_apic_id) << 24;

    ioapic_write(high_reg, high);
    ioapic_write(low_reg, low);
    return 0;
}
