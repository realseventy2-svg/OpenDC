#ifndef __BIOS_RENDERER_H
#define __BIOS_RENDERER_H

#include <stdint.h>
#include "config.h"

void renderer_set_fb(uint16_t *fb);
void draw_rect(int x, int y, int w, int h, uint16_t color);

/* Authentic Sega 12x24 BIOS Font (Primary UI Font) */
void draw_bfont_char(int x, int y, char c, uint16_t color);
void draw_bfont(int x, int y, uint16_t color, const char *str);
void draw_bfont_fmt(int x, int y, uint16_t color, const char *fmt, ...);
void draw_bfont_centered(int center_x, int y, uint16_t color, const char *str);
void draw_bfont_centered_fmt(int center_x, int y, uint16_t color, const char *fmt, ...);

/* Retro 8x8 Monospace System Font (For Memory Inspector, Hex Dumps, Dense Diagnostics) */
void draw_sysfont_char(int x, int y, char c, uint16_t color);
void draw_sysfont(int x, int y, uint16_t color, const char *str);
void draw_sysfont_fmt(int x, int y, uint16_t color, const char *fmt, ...);
void draw_sysfont_2x(int x, int y, uint16_t color, const char *str);
void draw_sysfont_2x_fmt(int x, int y, uint16_t color, const char *fmt, ...);

/* Default aliases */
void draw_text(int x, int y, uint16_t color, const char *str);
void draw_text_fmt(int x, int y, uint16_t color, const char *fmt, ...);
void draw_text_2x(int x, int y, uint16_t color, const char *str);
void draw_text_2x_fmt(int x, int y, uint16_t color, const char *fmt, ...);

#endif /* __BIOS_RENDERER_H */
