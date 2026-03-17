#include "keyboard.h"
#include "device.h"
#include <stddef.h>
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
static uint8_t left_ctrl_down;
static uint8_t right_ctrl_down;
static uint8_t extended_prefix;
static uint8_t keyboard_registered;
static uint8_t ctrl_c_pending;

#define KBD_QUEUE_SIZE 128u

static char g_queue[KBD_QUEUE_SIZE];
static uint32_t g_queue_head;
static uint32_t g_queue_tail;

static uint8_t queue_is_empty(void) {
    return g_queue_head == g_queue_tail;
}

static uint8_t queue_is_full(void) {
    return (uint32_t)((g_queue_tail + 1u) % KBD_QUEUE_SIZE) == g_queue_head;
}

static void queue_push(char c) {
    if (c == 0 || queue_is_full()) {
        return;
    }
    g_queue[g_queue_tail] = c;
    g_queue_tail = (uint32_t)((g_queue_tail + 1u) % KBD_QUEUE_SIZE);
}

static char queue_pop(void) {
    char c;
    if (queue_is_empty()) {
        return 0;
    }
    c = g_queue[g_queue_head];
    g_queue_head = (uint32_t)((g_queue_head + 1u) % KBD_QUEUE_SIZE);
    return c;
}

static void queue_drop_char(char target) {
    uint32_t out = g_queue_head;
    uint32_t in = g_queue_head;

    while (in != g_queue_tail) {
        char c = g_queue[in];
        in = (uint32_t)((in + 1u) % KBD_QUEUE_SIZE);
        if (c == target) {
            continue;
        }
        g_queue[out] = c;
        out = (uint32_t)((out + 1u) % KBD_QUEUE_SIZE);
    }
    g_queue_tail = out;
}

static void keyboard_pump(void) {
    while ((port_in8(0x64) & 0x01u) != 0u) {
        uint8_t scancode = port_in8(0x60);
        uint8_t released;
        uint8_t shift_on;
        uint8_t ctrl_on;
        char out = 0;
        uint8_t code;

        if (scancode == 0xE0u) {
            extended_prefix = 1u;
            continue;
        }

        released = (uint8_t)(scancode & 0x80u);
        code = (uint8_t)(scancode & 0x7Fu);

        if (extended_prefix) {
            extended_prefix = 0u;
            if (code == 0x1Du) {
                right_ctrl_down = (uint8_t)(released == 0u);
            } else if (!released) {
                if (code == 0x48u) {
                    queue_push(KEYBOARD_KEY_UP);
                } else if (code == 0x50u) {
                    queue_push(KEYBOARD_KEY_DOWN);
                } else if (code == 0x4Bu) {
                    queue_push(KEYBOARD_KEY_LEFT);
                } else if (code == 0x4Du) {
                    queue_push(KEYBOARD_KEY_RIGHT);
                }
            }
            continue;
        }

        if (code == 0x2Au) {
            left_shift_down = (uint8_t)(released == 0u);
            continue;
        }
        if (code == 0x36u) {
            right_shift_down = (uint8_t)(released == 0u);
            continue;
        }
        if (code == 0x1Du) {
            left_ctrl_down = (uint8_t)(released == 0u);
            continue;
        }
        if (released) {
            continue;
        }

        shift_on = (uint8_t)(left_shift_down || right_shift_down);
        ctrl_on = (uint8_t)(left_ctrl_down || right_ctrl_down);

        if (ctrl_on) {
            if (code == 0x2Eu) {
                ctrl_c_pending = 1u;
                queue_push(0x03);
            }
            continue;
        }

        out = shift_on ? keymap_shift[code] : keymap[code];
        if (out) {
            queue_push(out);
        }
    }
}

void keyboard_init(void) {
    if (keyboard_registered) {
        return;
    }

    if (device_register(MYAOS_DEV_INPUT, "kbd0", "i8042", NULL, NULL, NULL) == 0) {
        keyboard_registered = 1;
    }
}

char keyboard_read_char(void) {
    char c;
    keyboard_pump();
    c = queue_pop();
    if (c == 0x03) {
        ctrl_c_pending = 0u;
    }
    return c;
}

uint8_t keyboard_take_ctrl_c(void) {
    keyboard_pump();
    if (ctrl_c_pending == 0u) {
        return 0u;
    }
    ctrl_c_pending = 0u;
    queue_drop_char(0x03);
    return 1u;
}

void keyboard_reboot(void) {
    while (port_in8(0x64) & 0x02u) {
    }

    port_out8(0x64, 0xFEu);
}
