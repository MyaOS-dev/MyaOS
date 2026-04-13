#include "keyboard.h"
#include "console.h"
#include "device.h"
#include "scheduler.h"
#include "timer.h"
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

    [0x0F] = '\t',
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

    [0x0F] = '\t',
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
static uint8_t left_alt_down;
static uint8_t right_alt_down;
static uint8_t left_gui_down;
static uint8_t right_gui_down;
static uint8_t extended_prefix;
static uint8_t keyboard_registered;
static uint8_t ctrl_c_pending;

#define KBD_CHAR_QUEUE_SIZE 128u
#define KBD_EVENT_QUEUE_SIZE 256u
#define SCANCODE_F1 0x3Bu
#define SCANCODE_F6 0x40u

static char g_char_queue[KBD_CHAR_QUEUE_SIZE];
static uint32_t g_char_head;
static uint32_t g_char_tail;

static myaos_input_key_event_t g_event_queue[KBD_EVENT_QUEUE_SIZE];
static uint32_t g_event_head;
static uint32_t g_event_tail;

static uint8_t g_key_down[MYAOS_INPUT_KEYBOARD_KEYS];

static inline uint64_t irq_save_disable(void) {
    uint64_t flags;
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & 0x200ull) {
        __asm__ __volatile__("sti" : : : "memory");
    }
}

static uint8_t char_queue_is_empty(void) {
    return g_char_head == g_char_tail;
}

static uint8_t char_queue_is_full(void) {
    return (uint32_t)((g_char_tail + 1u) % KBD_CHAR_QUEUE_SIZE) == g_char_head;
}

static void char_queue_push(char c) {
    if (c == 0 || char_queue_is_full()) {
        return;
    }
    g_char_queue[g_char_tail] = c;
    g_char_tail = (uint32_t)((g_char_tail + 1u) % KBD_CHAR_QUEUE_SIZE);
}

static char char_queue_pop(void) {
    char c;

    if (char_queue_is_empty()) {
        return 0;
    }
    c = g_char_queue[g_char_head];
    g_char_head = (uint32_t)((g_char_head + 1u) % KBD_CHAR_QUEUE_SIZE);
    return c;
}

static void char_queue_drop_char(char target) {
    uint32_t out = g_char_head;
    uint32_t in = g_char_head;

    while (in != g_char_tail) {
        char c = g_char_queue[in];
        in = (uint32_t)((in + 1u) % KBD_CHAR_QUEUE_SIZE);
        if (c == target) {
            continue;
        }
        g_char_queue[out] = c;
        out = (uint32_t)((out + 1u) % KBD_CHAR_QUEUE_SIZE);
    }
    g_char_tail = out;
}

static uint8_t event_queue_is_empty(void) {
    return g_event_head == g_event_tail;
}

static uint8_t event_queue_is_full(void) {
    return (uint32_t)((g_event_tail + 1u) % KBD_EVENT_QUEUE_SIZE) == g_event_head;
}

static uint8_t event_queue_push(const myaos_input_key_event_t* ev) {
    if (!ev) {
        return 0u;
    }
    if (event_queue_is_full()) {
        g_event_head = (uint32_t)((g_event_head + 1u) % KBD_EVENT_QUEUE_SIZE);
    }
    g_event_queue[g_event_tail] = *ev;
    g_event_tail = (uint32_t)((g_event_tail + 1u) % KBD_EVENT_QUEUE_SIZE);
    return 1u;
}

static uint8_t event_queue_pop(myaos_input_key_event_t* out) {
    if (!out || event_queue_is_empty()) {
        return 0u;
    }
    *out = g_event_queue[g_event_head];
    g_event_head = (uint32_t)((g_event_head + 1u) % KBD_EVENT_QUEUE_SIZE);
    return 1u;
}

static uint8_t keyboard_modifiers(void) {
    uint8_t mods = 0u;

    if (left_shift_down || right_shift_down) {
        mods |= MYAOS_INPUT_MOD_SHIFT;
    }
    if (left_ctrl_down || right_ctrl_down) {
        mods |= MYAOS_INPUT_MOD_CTRL;
    }
    if (left_alt_down || right_alt_down) {
        mods |= MYAOS_INPUT_MOD_ALT;
    }
    if (left_gui_down || right_gui_down) {
        mods |= MYAOS_INPUT_MOD_GUI;
    }
    return mods;
}

static uint16_t keyboard_make_keycode(uint8_t code, uint8_t is_extended) {
    return (uint16_t)code | (is_extended ? 0x80u : 0u);
}

static uint32_t keyboard_pump_locked(void) {
    uint32_t emitted_events = 0u;

    while ((port_in8(0x64) & 0x01u) != 0u) {
        uint8_t scancode = port_in8(0x60);
        uint8_t released;
        uint8_t code;
        uint8_t is_extended = 0u;
        uint16_t keycode;
        uint8_t action;
        uint8_t shift_on;
        uint8_t ctrl_on;
        uint32_t ascii = 0u;
        myaos_input_key_event_t ev;

        if (scancode == 0xE0u) {
            extended_prefix = 1u;
            continue;
        }

        released = (uint8_t)(scancode & 0x80u);
        code = (uint8_t)(scancode & 0x7Fu);
        is_extended = extended_prefix;
        extended_prefix = 0u;
        keycode = keyboard_make_keycode(code, is_extended);
        if (keycode >= MYAOS_INPUT_KEYBOARD_KEYS) {
            continue;
        }

        if (!released) {
            action = g_key_down[keycode] ? MYAOS_INPUT_KEY_EVENT_REPEAT : MYAOS_INPUT_KEY_EVENT_PRESS;
            g_key_down[keycode] = 1u;
        } else {
            action = MYAOS_INPUT_KEY_EVENT_RELEASE;
            g_key_down[keycode] = 0u;
        }

        if (!is_extended && code == 0x2Au) {
            left_shift_down = (uint8_t)(released == 0u);
        } else if (!is_extended && code == 0x36u) {
            right_shift_down = (uint8_t)(released == 0u);
        } else if (!is_extended && code == 0x1Du) {
            left_ctrl_down = (uint8_t)(released == 0u);
        } else if (is_extended && code == 0x1Du) {
            right_ctrl_down = (uint8_t)(released == 0u);
        } else if (!is_extended && code == 0x38u) {
            left_alt_down = (uint8_t)(released == 0u);
        } else if (is_extended && code == 0x38u) {
            right_alt_down = (uint8_t)(released == 0u);
        } else if (is_extended && code == 0x5Bu) {
            left_gui_down = (uint8_t)(released == 0u);
        } else if (is_extended && code == 0x5Cu) {
            right_gui_down = (uint8_t)(released == 0u);
        }

        if (!released) {
            shift_on = (uint8_t)(left_shift_down || right_shift_down);
            ctrl_on = (uint8_t)(left_ctrl_down || right_ctrl_down);
            if (!is_extended &&
                action == MYAOS_INPUT_KEY_EVENT_PRESS &&
                (left_alt_down || right_alt_down) &&
                code >= SCANCODE_F1 && code <= SCANCODE_F6) {
                (void)console_tty_switch((uint32_t)(code - SCANCODE_F1));
            }

            if (is_extended) {
                if (code == 0x48u) {
                    char_queue_push(KEYBOARD_KEY_UP);
                } else if (code == 0x50u) {
                    char_queue_push(KEYBOARD_KEY_DOWN);
                } else if (code == 0x4Bu) {
                    char_queue_push(KEYBOARD_KEY_LEFT);
                } else if (code == 0x4Du) {
                    char_queue_push(KEYBOARD_KEY_RIGHT);
                }
            } else if (ctrl_on) {
                if (code == 0x2Eu) {
                    ctrl_c_pending = 1u;
                    char_queue_push(0x03);
                    ascii = 0x03u;
                }
            } else {
                char out = shift_on ? keymap_shift[code] : keymap[code];
                if (out != 0) {
                    char_queue_push(out);
                    ascii = (uint32_t)(uint8_t)out;
                }
            }
        }

        ev.keycode = keycode;
        ev.action = action;
        ev.modifiers = keyboard_modifiers();
        ev.ascii = ascii;
        ev.tick = timer_ticks();
        if (event_queue_push(&ev)) {
            emitted_events++;
        }
    }

    return emitted_events;
}

static uint32_t keyboard_pump(void) {
    uint64_t irq_flags = irq_save_disable();
    uint32_t emitted = keyboard_pump_locked();
    irq_restore(irq_flags);
    return emitted;
}

void keyboard_init(void) {
    if (keyboard_registered) {
        return;
    }

    if (device_register(MYAOS_DEV_INPUT, "kbd0", "i8042", NULL, NULL, NULL) == 0) {
        keyboard_registered = 1;
    }
}

void keyboard_on_irq(void) {
    if (keyboard_pump() != 0u) {
        scheduler_notify_all(MYAOS_EVENT_INPUT_KEYBOARD);
    }
}

char keyboard_read_char(void) {
    uint64_t irq_flags;
    char c;

    (void)keyboard_pump();
    irq_flags = irq_save_disable();
    c = char_queue_pop();
    if (c == 0x03) {
        ctrl_c_pending = 0u;
    }
    irq_restore(irq_flags);
    return c;
}

int keyboard_key_state(uint16_t keycode) {
    uint64_t irq_flags;
    int pressed;

    if (keycode >= MYAOS_INPUT_KEYBOARD_KEYS) {
        return -1;
    }

    (void)keyboard_pump();
    irq_flags = irq_save_disable();
    pressed = g_key_down[keycode] ? 1 : 0;
    irq_restore(irq_flags);
    return pressed;
}

int keyboard_keyboard_state(uint8_t* out_keys, uint32_t out_size) {
    uint64_t irq_flags;

    if (!out_keys || out_size < MYAOS_INPUT_KEYBOARD_KEYS) {
        return -1;
    }

    (void)keyboard_pump();
    irq_flags = irq_save_disable();
    for (uint32_t i = 0u; i < MYAOS_INPUT_KEYBOARD_KEYS; i++) {
        out_keys[i] = g_key_down[i];
    }
    irq_restore(irq_flags);
    return (int)MYAOS_INPUT_KEYBOARD_KEYS;
}

int keyboard_key_event_read(myaos_input_key_event_t* out_event) {
    uint64_t irq_flags;
    uint8_t ok;

    if (!out_event) {
        return -1;
    }

    (void)keyboard_pump();
    irq_flags = irq_save_disable();
    ok = event_queue_pop(out_event);
    irq_restore(irq_flags);
    return ok ? 1 : 0;
}

uint32_t keyboard_event_pending(void) {
    uint64_t irq_flags;
    uint32_t pending;

    (void)keyboard_pump();
    irq_flags = irq_save_disable();
    if (g_event_tail >= g_event_head) {
        pending = g_event_tail - g_event_head;
    } else {
        pending = KBD_EVENT_QUEUE_SIZE - g_event_head + g_event_tail;
    }
    irq_restore(irq_flags);
    return pending;
}

uint8_t keyboard_take_ctrl_c(void) {
    uint64_t irq_flags;
    uint8_t had_ctrl_c;

    (void)keyboard_pump();
    irq_flags = irq_save_disable();
    had_ctrl_c = ctrl_c_pending;
    if (had_ctrl_c) {
        ctrl_c_pending = 0u;
        char_queue_drop_char(0x03);
    }
    irq_restore(irq_flags);
    return had_ctrl_c;
}

void keyboard_reboot(void) {
    while (port_in8(0x64) & 0x02u) {
    }

    port_out8(0x64, 0xFEu);
}
