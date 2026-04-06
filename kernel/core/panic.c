#include "panic.h"
#include "console.h"

static void u64_to_hex(uint64_t value, char* out, uint32_t out_size) {
    static const char hex[] = "0123456789ABCDEF";
    uint32_t nibbles = 16u;
    uint32_t pos = 0;

    if (!out || out_size < 3u) {
        return;
    }

    out[pos++] = '0';
    out[pos++] = 'x';
    if (out_size < 2u + nibbles + 1u) {
        nibbles = out_size - 3u;
    }

    for (uint32_t i = 0; i < nibbles; i++) {
        uint32_t shift = (nibbles - 1u - i) * 4u;
        out[pos++] = hex[(value >> shift) & 0xFu];
    }
    out[pos] = '\0';
}

static __attribute__((noreturn)) void panic_halt(void) {
    for (;;) {
        __asm__ __volatile__("cli; hlt");
    }
}

void panic_message(const char* message) {
    console_write("\nPANIC: ");
    console_write(message ? message : "unknown");
    console_write("\n");
    panic_halt();
}

void panic_exception(uint64_t vector, uint64_t error_code, uint64_t rip) {
    char hex_buf[32];

    console_write("\nPANIC: unhandled kernel exception\n");
    console_write("vector=");
    console_write_u64(vector);
    console_write(" error=");
    console_write_u64(error_code);
    console_write(" rip=");
    u64_to_hex(rip, hex_buf, sizeof(hex_buf));
    console_write(hex_buf);
    console_write("\n");
    panic_halt();
}
