#ifndef OPENDC_BOOTLOADER_VIDEO_H
#define OPENDC_BOOTLOADER_VIDEO_H

#include <stdint.h>
#include <stddef.h>

#define SCREEN_WIDTH        640
#define SCREEN_HEIGHT       480
#define VRAM_BASE           0xA5000000UL

/* RGB565 Color Helper */
#define RGB565(r, g, b)     ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

#define COLOR_BLACK         RGB565(0,   0,   0)
#define COLOR_WHITE         RGB565(255, 255, 255)
#define COLOR_CYAN          RGB565(0,   220, 255)
#define COLOR_GREEN         RGB565(40,  255, 120)
#define COLOR_GOLD          RGB565(255, 200, 40)
#define COLOR_RED           RGB565(255, 60,  60)
#define COLOR_BLUE          RGB565(60,  120, 255)
#define COLOR_DARK_GRAY     RGB565(40,  40,  40)
#define COLOR_LIGHT_GRAY    RGB565(180, 180, 180)

void video_init(void);
void video_wait_vblank(void);
void video_wait_seconds(int seconds);
void video_clear(uint16_t color);
void video_fill_rect(int x, int y, int w, int h, uint16_t color);
void video_draw_pixel(int x, int y, uint16_t color);
void video_draw_line(int x0, int y0, int x1, int y1, uint16_t color);
void video_draw_char(int x, int y, char c, uint16_t color, int scale);
void video_draw_string(int x, int y, const char *str, uint16_t color, int scale);
void video_draw_string_centered(int center_x, int y, const char *str, uint16_t color, int scale);
void video_draw_hex32(int x, int y, uint32_t value, uint16_t color, int scale);
void video_draw_hex8(int x, int y, const uint8_t *data, uint16_t color, int scale);

#endif /* OPENDC_BOOTLOADER_VIDEO_H */
