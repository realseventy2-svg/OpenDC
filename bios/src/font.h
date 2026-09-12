#ifndef __BIOS_FONT_H
#define __BIOS_FONT_H

#include <stdint.h>
#include "config.h"

void font_set_fb(uint16_t *fb);
void draw_rect(int x, int y, int w, int h, uint16_t color);
void draw_char_1x(int x, int y, char c, uint16_t color);
void draw_char_2x(int x, int y, char c, uint16_t color);
void draw_text(int x, int y, uint16_t color, const char *str);
void draw_text_fmt(int x, int y, uint16_t color, const char *fmt, ...);
void draw_text_2x(int x, int y, uint16_t color, const char *str);
void draw_text_2x_fmt(int x, int y, uint16_t color, const char *fmt, ...);
void get_clean_str(char *dst, const char *src, int max_len);

#endif /* __BIOS_FONT_H */
