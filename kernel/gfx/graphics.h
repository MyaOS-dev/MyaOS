#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "boot.h"
#include <stdint.h>

#define FONT_WIDTH  8
#define FONT_HEIGHT 16

void graphics_set_mode(boot_info_t* boot, uint32_t mode);
int graphics_present(boot_info_t* boot);
uint32_t* graphics_draw_buffer(boot_info_t* boot);

void put_pixel(boot_info_t* boot, uint32_t x, uint32_t y, uint32_t color);
void clear_screen(boot_info_t* boot, uint32_t color);
void draw_line(boot_info_t* boot, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void fill_rect(boot_info_t* boot, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color);
void draw_rect_frame(boot_info_t* boot, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t color);

void draw_char(boot_info_t* boot, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void draw_string(boot_info_t* boot, uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg);

void draw_symbol(boot_info_t* boot, int xcord, int ycord);

#endif
