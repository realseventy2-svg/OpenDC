#ifndef __BIOS_RENDERER_H
#define __BIOS_RENDERER_H

#include <stdint.h>
#include "config.h"

void renderer_set_fb(uint16_t *fb);
void draw_rect(int x, int y, int w, int h, uint16_t color);
void draw_char_1x(int x, int y, char c, uint16_t color);
void draw_char_2x(int x, int y, char c, uint16_t color);
void draw_text(int x, int y, uint16_t color, const char *str);
void draw_text_fmt(int x, int y, uint16_t color, const char *fmt, ...);
void draw_text_2x(int x, int y, uint16_t color, const char *str);
void draw_text_2x_fmt(int x, int y, uint16_t color, const char *fmt, ...);

#endif /* __BIOS_RENDERER_H */
