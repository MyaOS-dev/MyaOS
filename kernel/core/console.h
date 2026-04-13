#ifndef CONSOLE_H
#define CONSOLE_H

#include "boot.h"
#include <stddef.h>
#include <stdint.h>

#define CONSOLE_MODE_TEXT 0u
#define CONSOLE_MODE_GRAPHICS 1u
#define CONSOLE_TTY_MAX 6u

void console_init(boot_info_t* boot, uint32_t fg, uint32_t bg);
void console_reset(void);
void console_put_char(char c);
void console_write(const char* text);
void console_write_len(const char* text, size_t len);
void console_write_u32(uint32_t value);
void console_write_u64(uint64_t value);
void console_write_binary_as_text(const uint8_t* data, uint32_t size);
void console_get_cursor(uint32_t* out_col, uint32_t* out_row);
uint32_t console_cols(void);
void console_draw_cell(uint32_t col, uint32_t row, char c);
void console_set_mode(uint32_t mode);
uint32_t console_get_mode(void);
uint32_t console_tty_count(void);
uint32_t console_tty_active(void);
int console_tty_switch(uint32_t index);
boot_info_t* console_boot_info(void);

#endif
