#include "interrupts.h"
#include "panic.h"
#include "scheduler.h"
#include "syscall.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} idtr_t;

extern void* isr_stub_table[256];

static idt_entry_t g_idt[256];

typedef struct {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
} interrupt_context_t;

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_wait(void) {
    out8(0x80u, 0u);
}

static inline void lidt_load(const idtr_t* idtr) {
    __asm__ __volatile__("lidt (%0)" : : "r"(idtr) : "memory");
}

static inline uint16_t read_cs(void) {
    uint16_t cs;
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    return cs;
}

static void idt_set_entry(uint8_t vector, void* handler, uint16_t selector, uint8_t type_attr) {
    uint64_t addr = (uint64_t)(uintptr_t)handler;
    g_idt[vector].offset_low = (uint16_t)(addr & 0xFFFFu);
    g_idt[vector].selector = selector;
    g_idt[vector].ist = 0u;
    g_idt[vector].type_attr = type_attr;
    g_idt[vector].offset_mid = (uint16_t)((addr >> 16) & 0xFFFFu);
    g_idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFFu);
    g_idt[vector].zero = 0u;
}

static void pic_remap_and_mask(void) {
    uint8_t mask_master = in8(0x21u);
    uint8_t mask_slave = in8(0xA1u);

    out8(0x20u, 0x11u);
    io_wait();
    out8(0xA0u, 0x11u);
    io_wait();

    out8(0x21u, 0x20u);
    io_wait();
    out8(0xA1u, 0x28u);
    io_wait();

    out8(0x21u, 0x04u);
    io_wait();
    out8(0xA1u, 0x02u);
    io_wait();

    out8(0x21u, 0x01u);
    io_wait();
    out8(0xA1u, 0x01u);
    io_wait();

    (void)mask_master;
    (void)mask_slave;

    out8(0x21u, 0xFEu);
    out8(0xA1u, 0xFFu);
}

uint64_t interrupt_dispatch(uint64_t vector, uint64_t error_code, uint64_t frame_rsp) {
    (void)error_code;

    uint64_t next_rsp = frame_rsp;
    interrupt_context_t* frame = (interrupt_context_t*)(uintptr_t)frame_rsp;
    uint8_t from_user = (frame && ((frame->cs & 0x3u) == 0x3u)) ? 1u : 0u;

    if (vector == 32u) {
        timer_irq_tick();
        next_rsp = scheduler_on_timer_interrupt(frame_rsp, timer_ticks());
        if (next_rsp == 0) {
            next_rsp = frame_rsp;
        }
    } else if (vector == 0x80u) {
        if (frame) {
            frame->rax = (uint64_t)syscall_dispatch(
                frame->rax,
                frame->rbx,
                frame->rcx,
                frame->rdx,
                frame->rsi,
                frame->rdi
            );
            if (scheduler_reschedule_pending()) {
                next_rsp = scheduler_on_syscall_complete(frame_rsp, timer_ticks());
                if (next_rsp == 0) {
                    next_rsp = frame_rsp;
                }
            }
        }
    } else if (vector < 32u) {
        if (from_user) {
            scheduler_kill_current(-(int32_t)(0x80u + vector));
            if (scheduler_reschedule_pending()) {
                next_rsp = scheduler_on_syscall_complete(frame_rsp, timer_ticks());
                if (next_rsp == 0) {
                    next_rsp = frame_rsp;
                }
            }
        } else {
            uint64_t rip = frame ? frame->rip : 0u;
            panic_exception(vector, error_code, rip);
        }
    }

    if (vector >= 32u && vector < 48u) {
        if (vector >= 40u) {
            out8(0xA0u, 0x20u);
        }
        out8(0x20u, 0x20u);
    }

    return next_rsp;
}

void interrupts_init(void) {
    uint16_t cs = read_cs();
    for (uint32_t i = 0; i < 256u; i++) {
        idt_set_entry((uint8_t)i, isr_stub_table[i], cs, 0x8Eu);
    }
    idt_set_entry(0x80u, isr_stub_table[0x80], cs, 0xEEu);

    idtr_t idtr;
    idtr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    idtr.base = (uint64_t)(uintptr_t)&g_idt[0];
    lidt_load(&idtr);

    pic_remap_and_mask();
}

void interrupts_enable(void) {
    __asm__ __volatile__("sti");
}

void interrupts_disable(void) {
    __asm__ __volatile__("cli");
}

uint64_t interrupts_timer_ticks(void) {
    return timer_ticks();
}
