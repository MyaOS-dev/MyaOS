#include "keyboard.h"
#include <stdint.h>

static inline uint8_t port_in8(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void port_out8(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static const char keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',

    [0x0E] = '\b',
    [0x1C] = '\n',

    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',

    [0x39] = ' ',
};

static const char keymap_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',

    [0x0E] = '\b',
    [0x1C] = '\n',

    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M',
    [0x33] = '<', [0x34] = '>', [0x35] = '?',

    [0x39] = ' ',
};

static uint8_t left_shift_down;
static uint8_t right_shift_down;
static uint8_t skip_extended;

char keyboard_read_char(void) {
    if ((port_in8(0x64) & 0x01u) == 0) {
        return 0;
    }

    uint8_t scancode = port_in8(0x60);

    if (scancode == 0xE0 || scancode == 0xE1) {
        skip_extended = 1;
        return 0;
    }

    if (skip_extended) {
        skip_extended = 0;
        return 0;
    }

    uint8_t released = (uint8_t)(scancode & 0x80u);
    scancode &= 0x7Fu;

    if (scancode == 0x2A) {
        left_shift_down = (uint8_t)(released == 0);
        return 0;
    }

    if (scancode == 0x36) {
        right_shift_down = (uint8_t)(released == 0);
        return 0;
    }

    if (released) {
        return 0;
    }

    uint8_t shift_on = (uint8_t)(left_shift_down || right_shift_down);
    return shift_on ? keymap_shift[scancode] : keymap[scancode];
}

void keyboard_reboot(void) {
    while (port_in8(0x64) & 0x02u) {
    }

    port_out8(0x64, 0xFEu);
}
