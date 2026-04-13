#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <myaos/syscall.h>
#include <stdint.h>

#define KEYBOARD_KEY_UP ((char)0x11)
#define KEYBOARD_KEY_DOWN ((char)0x12)
#define KEYBOARD_KEY_LEFT ((char)0x13)
#define KEYBOARD_KEY_RIGHT ((char)0x14)

void keyboard_init(void);
void keyboard_on_irq(void);
char keyboard_read_char(void);
int keyboard_key_state(uint16_t keycode);
int keyboard_keyboard_state(uint8_t* out_keys, uint32_t out_size);
int keyboard_key_event_read(myaos_input_key_event_t* out_event);
uint32_t keyboard_event_pending(void);
uint8_t keyboard_take_ctrl_c(void);
void keyboard_reboot(void);

#endif
