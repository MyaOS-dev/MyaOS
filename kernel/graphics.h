#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "boot.h"
#include <stdint.h>

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

void put_pixel(boot_info_t* boot, uint32_t x, uint32_t y, uint32_t color);
void clear_screen(boot_info_t* boot, uint32_t color);

void draw_char(boot_info_t* boot, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void draw_string(boot_info_t* boot, uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);

void draw_symbol(boot_info_t* boot, int xcord, int ycord);

#endif