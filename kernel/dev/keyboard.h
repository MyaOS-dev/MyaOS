#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

#define KEYBOARD_KEY_UP ((char)0x11)
#define KEYBOARD_KEY_DOWN ((char)0x12)
#define KEYBOARD_KEY_LEFT ((char)0x13)
#define KEYBOARD_KEY_RIGHT ((char)0x14)

void keyboard_init(void);
char keyboard_read_char(void);
uint8_t keyboard_take_ctrl_c(void);
void keyboard_reboot(void);

#endif
