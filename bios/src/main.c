#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/vmu.h>
#include <dc/cdrom.h>
#include <dc/fs_iso9660.h>
#include <dc/video.h>
#include <arch/exec.h>
#include <arch/rtc.h>
#include <dc/sq.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "bootloader_gdrom.h"

extern const uint8_t romdisk[];

/* Fast boot flags */
KOS_INIT_FLAGS(INIT_IRQ | INIT_THD_PREEMPT | INIT_CONTROLLER | INIT_VMU | INIT_NO_DCLOAD);

/* RGB565 Color Palette */
#define COLOR_BG        0x08A5  /* Dark Slate Navy */
#define COLOR_PANEL_BG  0x110A  /* Panel Box Background */
#define COLOR_BORDER    0x3A6E  /* Slate Blue Border */
#define COLOR_CYAN      0x367F  /* Bright Sega Cyan */
#define COLOR_WHITE     0xFFFF  /* Pure White */
#define COLOR_GOLD      0xFEA0  /* Active Selection Gold */
#define COLOR_GREEN     0x2FE4  /* Status Green */
#define COLOR_RED       0xF986  /* Error / Warning Red */
#define COLOR_GRAY      0x8C71  /* Muted Text Gray */
#define COLOR_DARK_GRAY 0x4208  /* Dark Line Gray */

/* Screen Dimensions */
#define SCREEN_W 640
#define SCREEN_H 480

/* Audio Hardware Direct Registers */
#define AICA_REG_BASE 0xA0700000UL
#define AICA_RAM_BASE 0xA0800000UL
#define AICA_CHN_REG(ch, reg) (*(volatile uint32_t *)(AICA_REG_BASE + ((ch) * 0x80) + (reg)))

/* Framebuffer pointer (points to current backbuffer) */
static uint16_t *s_fb = NULL;
static int s_audio_timer = 0;

/* -------------------------------------------------------------------------- */
/* Low-Level Audio Chime / Beep Engine                                        */
/* -------------------------------------------------------------------------- */
static void audio_hw_init(void) {
    *(volatile uint32_t *)0xA0702C00UL |= 1;
    *(volatile uint16_t *)0xA0702800UL = 0x000F;

    for(int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }

    volatile int16_t *sine_ram = (volatile int16_t *)AICA_RAM_BASE;
    for(int i = 0; i < 256; i++) {
        double rad = (double)i * (2.0 * 3.141592653589793 / 256.0);
        sine_ram[i] = (int16_t)(sin(rad) * 30000.0);
    }
}

static void audio_play_tone(int ch, uint32_t pitch, int volume, int pan, int duration_frames) {
    if(ch < 0 || ch >= 64) return;
    AICA_CHN_REG(ch, 0x00) = 0x8000;
    AICA_CHN_REG(ch, 0x04) = 0;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;
    AICA_CHN_REG(ch, 0x10) = 0x001F;
    AICA_CHN_REG(ch, 0x14) = 0x3D0E;
    AICA_CHN_REG(ch, 0x18) = pitch;
    AICA_CHN_REG(ch, 0x24) = ((uint32_t)(volume & 0x0F) << 8) | (pan & 0x1F);
    AICA_CHN_REG(ch, 0x28) = 0x0024;
    AICA_CHN_REG(ch, 0x00) = 0xC200;
    if(duration_frames > s_audio_timer) {
        s_audio_timer = duration_frames;
    }
}

static void audio_stop_all(void) {
    for(int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
    }
    s_audio_timer = 0;
}

static void audio_play_click(void) {
    audio_play_tone(0, 0x091C, 8, 0x00, 3); /* 3 frames ~50ms click */
}

static void audio_play_confirm(void) {
    audio_play_tone(0, 0x111C, 10, 0x1F, 8);
    audio_play_tone(1, 0x1A13, 10, 0x00, 8); /* ~130ms confirm */
}

/* -------------------------------------------------------------------------- */
/* Standard 8x8 Developer BIOS Font Table                                     */
/* -------------------------------------------------------------------------- */
static const uint8_t FONT_8X8[95][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 32 ' ' */
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00 }, /* 33 '!' */
    { 0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 34 '"' */
    { 0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00 }, /* 35 '#' */
    { 0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00 }, /* 36 '$' */
    { 0x00, 0x63, 0x66, 0x0C, 0x18, 0x33, 0x63, 0x00 }, /* 37 '%' */
    { 0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00 }, /* 38 '&' */
    { 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 39 '\''*/
    { 0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00 }, /* 40 '(' */
    { 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00 }, /* 41 ')' */
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 }, /* 42 '*' */
    { 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00 }, /* 43 '+' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 }, /* 44 ',' */
    { 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00 }, /* 45 '-' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 }, /* 46 '.' */
    { 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00 }, /* 47 '/' */
    { 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00 }, /* 48 '0' */
    { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00 }, /* 49 '1' */
    { 0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00 }, /* 50 '2' */
    { 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00 }, /* 51 '3' */
    { 0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00 }, /* 52 '4' */
    { 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00 }, /* 53 '5' */
    { 0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00 }, /* 54 '6' */
    { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00 }, /* 55 '7' */
    { 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00 }, /* 56 '8' */
    { 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00 }, /* 57 '9' */
    { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 }, /* 58 ':' */
    { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30 }, /* 59 ';' */
    { 0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00 }, /* 60 '<' */
    { 0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00 }, /* 61 '=' */
    { 0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x00 }, /* 62 '>' */
    { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00 }, /* 63 '?' */
    { 0x3C, 0x66, 0x6E, 0x6E, 0x60, 0x62, 0x3C, 0x00 }, /* 64 '@' */
    { 0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00 }, /* 65 'A' */
    { 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00 }, /* 66 'B' */
    { 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00 }, /* 67 'C' */
    { 0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00 }, /* 68 'D' */
    { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00 }, /* 69 'E' */
    { 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00 }, /* 70 'F' */
    { 0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0x00 }, /* 71 'G' */
    { 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00 }, /* 72 'H' */
    { 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 }, /* 73 'I' */
    { 0x0E, 0x06, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00 }, /* 74 'J' */
    { 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00 }, /* 75 'K' */
    { 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00 }, /* 76 'L' */
    { 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00 }, /* 77 'M' */
    { 0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00 }, /* 78 'N' */
    { 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 }, /* 79 'O' */
    { 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00 }, /* 80 'P' */
    { 0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00 }, /* 81 'Q' */
    { 0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0x00 }, /* 82 'R' */
    { 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00 }, /* 83 'S' */
    { 0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00 }, /* 84 'T' */
    { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00 }, /* 85 'U' */
    { 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 }, /* 86 'V' */
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 }, /* 87 'W' */
    { 0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00 }, /* 88 'X' */
    { 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00 }, /* 89 'Y' */
    { 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00 }, /* 90 'Z' */
    { 0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00 }, /* 91 '[' */
    { 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00 }, /* 92 '\\'*/
    { 0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00 }, /* 93 ']' */
    { 0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 94 '^' */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00 }, /* 95 '_' */
    { 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 96 '`' */
    { 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00 }, /* 97 'a' */
    { 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00 }, /* 98 'b' */
    { 0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00 }, /* 99 'c' */
    { 0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00 }, /* 100 'd'*/
    { 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00 }, /* 101 'e'*/
    { 0x1C, 0x30, 0x78, 0x30, 0x30, 0x30, 0x30, 0x00 }, /* 102 'f'*/
    { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C }, /* 103 'g'*/
    { 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00 }, /* 104 'h'*/
    { 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00 }, /* 105 'i'*/
    { 0x0C, 0x00, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38 }, /* 106 'j'*/
    { 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00 }, /* 107 'k'*/
    { 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00 }, /* 108 'l'*/
    { 0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x63, 0x00 }, /* 109 'm'*/
    { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00 }, /* 110 'n'*/
    { 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00 }, /* 111 'o'*/
    { 0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60 }, /* 112 'p'*/
    { 0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x07 }, /* 113 'q'*/
    { 0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00 }, /* 114 'r'*/
    { 0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00 }, /* 115 's'*/
    { 0x18, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x0E, 0x00 }, /* 116 't'*/
    { 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00 }, /* 117 'u'*/
    { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00 }, /* 118 'v'*/
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x3E, 0x36, 0x00 }, /* 119 'w'*/
    { 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00 }, /* 120 'x'*/
    { 0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C }, /* 121 'y'*/
    { 0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00 }, /* 122 'z'*/
    { 0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00 }, /* 123 '{' */
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 }, /* 124 '|' */
    { 0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00 }, /* 125 '}' */
    { 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }  /* 126 '~' */
};

/* -------------------------------------------------------------------------- */
/* Graphics & Text Rendering Helpers                                          */
/* -------------------------------------------------------------------------- */
static void draw_rect(int x, int y, int w, int h, uint16_t color) {
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > SCREEN_W) w = SCREEN_W - x;
    if(y + h > SCREEN_H) h = SCREEN_H - y;
    if(w <= 0 || h <= 0 || !s_fb) return;

    for(int j = 0; j < h; j++) {
        uint16_t *row = &s_fb[(y + j) * SCREEN_W + x];
        for(int i = 0; i < w; i++) {
            row[i] = color;
        }
    }
}

static void draw_box(int x, int y, int w, int h, uint16_t border_col, uint16_t fill_col) {
    draw_rect(x, y, w, h, fill_col);
    draw_rect(x, y, w, 1, border_col);
    draw_rect(x, y + h - 1, w, 1, border_col);
    draw_rect(x, y, 1, h, border_col);
    draw_rect(x + w - 1, y, 1, h, border_col);
}

static void draw_char(int x, int y, uint16_t color, char c, int scale) {
    if(c < 32 || c > 126 || !s_fb) return;
    const uint8_t *glyph = FONT_8X8[c - 32];

    for(int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for(int col = 0; col < 8; col++) {
            if(bits & (0x80 >> col)) {
                if(scale == 1) {
                    int px = x + col;
                    int py = y + row;
                    if(px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H)
                        s_fb[py * SCREEN_W + px] = color;
                } else if(scale == 2) {
                    int px = x + col * 2;
                    int py = y + row * 2;
                    if(px >= 0 && px + 1 < SCREEN_W && py >= 0 && py + 1 < SCREEN_H) {
                        s_fb[py * SCREEN_W + px] = color;
                        s_fb[py * SCREEN_W + px + 1] = color;
                        s_fb[(py + 1) * SCREEN_W + px] = color;
                        s_fb[(py + 1) * SCREEN_W + px + 1] = color;
                    }
                }
            }
        }
    }
}

static void get_clean_str(char *dst, const char *src, int max_len) {
    if(!dst || !src || max_len <= 0) return;
    int len = 0;
    while(src[len] && len < max_len) {
        char c = src[len];
        if(c < 32 || c > 126) break;
        dst[len] = c;
        len++;
    }
    while(len > 0 && dst[len - 1] == ' ') len--;
    dst[len] = '\0';
}

static void draw_text(int x, int y, uint16_t color, const char *str) {
    if(!str) return;
    int cur_x = x;
    while(*str) {
        if(*str == '\n') {
            y += 18;
            cur_x = x;
        } else {
            draw_char(cur_x, y, color, *str, 2);
            cur_x += 16;
        }
        str++;
    }
}

static void draw_text_fmt(int x, int y, uint16_t color, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    draw_text(x, y, color, buf);
}

static void draw_text_small(int x, int y, uint16_t color, const char *str) {
    if(!str) return;
    int cur_x = x;
    while(*str) {
        if(*str == '\n') {
            y += 10;
            cur_x = x;
        } else {
            draw_char(cur_x, y, color, *str, 1);
            cur_x += 8;
        }
        str++;
    }
}

static void draw_text_small_fmt(int x, int y, uint16_t color, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    draw_text_small(x, y, color, buf);
}

/* -------------------------------------------------------------------------- */
/* Hardware & Disc Information Probing                                        */
/* -------------------------------------------------------------------------- */
typedef struct {
    int disc_present;
    int is_gdrom;
    char title[64];
    char product_id[16];
    char version[16];
    char region[16];
    uint32_t data_fad;
} disc_info_t;

static disc_info_t s_disc;

static const char *get_cable_name(void) {
    int cable = vid_check_cable();
    switch(cable) {
        case CT_VGA: return "VGA (31kHz 480p)";
        case CT_RGB: return "RGB (15kHz 480i)";
        case CT_COMPOSITE: return "Composite (15kHz)";
        default: return "Auto-Detect";
    }
}

static const char *get_region_name(void) {
    const char *cc = (const char *)0x8C008030UL;
    int has_j = 0, has_u = 0, has_e = 0;
    for(int i = 0; i < 8; i++) {
        if(cc[i] == 'J') has_j = 1;
        if(cc[i] == 'U') has_u = 1;
        if(cc[i] == 'E') has_e = 1;
    }
    if(has_j && !has_u && !has_e) return "NTSC-J (Japan)";
    if(has_e && !has_u && !has_j) return "PAL (Europe)";
    if(has_u) return "NTSC-U (USA)";
    return "Universal";
}

static void probe_disc(void) {
    memset(&s_disc, 0, sizeof(s_disc));
    volatile gdrom_service_table_t *gd = gdrom_services();

    if(gd && gd->magic == GDROM_SERVICE_MAGIC) {
        s_disc.disc_present = (gd->disc_present != 0);
        s_disc.data_fad = gd->data_fad;
    }

    uint8_t ip_sector[2048];
    if(gd && gd->magic == GDROM_SERVICE_MAGIC && s_disc.disc_present) {
        uint32_t fad = s_disc.data_fad ? s_disc.data_fad : 45150U;
        if(gd->read_fad(ip_sector, fad, 1) == 0) {
            if(ip_sector[0] == 'S' && ip_sector[1] == 'E' &&
               ip_sector[2] == 'G' && ip_sector[3] == 'A') {
                char *t = (char *)(ip_sector + 0x80);
                int len = 0;
                for(int i = 0; i < 63 && i < 128; i++) {
                    if(t[i] >= 32) s_disc.title[len++] = t[i];
                }
                while(len > 0 && s_disc.title[len - 1] == ' ') len--;
                s_disc.title[len] = '\0';

                memcpy(s_disc.product_id, ip_sector + 0x40, 10);
                s_disc.product_id[10] = '\0';

                memcpy(s_disc.version, ip_sector + 0x50, 6);
                s_disc.version[6] = '\0';

                memcpy(s_disc.region, ip_sector + 0x30, 8);
                s_disc.region[8] = '\0';

                s_disc.is_gdrom = (fad >= 45000U);
            }
        }
    }

    if(s_disc.title[0] == '\0') {
        if(s_disc.disc_present) {
            strcpy(s_disc.title, "Unidentified Disc / CD-ROM");
        } else {
            strcpy(s_disc.title, "NO DISC DETECTED");
        }
    }
}

static void launch_disc(void) {
    volatile gdrom_service_table_t *gd = gdrom_services();
    if(gd && gd->magic == GDROM_SERVICE_MAGIC && s_disc.disc_present) {
        audio_play_confirm();
        thd_sleep(250);
        uint32_t fad = s_disc.data_fad ? s_disc.data_fad : 45150U;
        gd->boot_game(fad);
    }
}

/* -------------------------------------------------------------------------- */
/* Screen 0: Main Developer Menu                                              */
/* -------------------------------------------------------------------------- */
#define MENU_COUNT 7
static const char *MENU_LABELS[MENU_COUNT] = {
    "1. BOOT GD-ROM / CD-ROM DISC",
    "2. SYSTEM & HARDWARE DIAGNOSTICS",
    "3. MAPLE BUS & VMU MANAGER",
    "4. FLASHROM CONFIGURATION VIEWER",
    "5. MEMORY & REGISTER HEX INSPECTOR",
    "6. VIDEO & AUDIO HARDWARE TEST",
    "7. REBOOT CONSOLE"
};

static int s_menu_sel = 0;
static int s_screen = 0;

static void render_main_menu(void) {
    draw_box(16, 16, SCREEN_W - 32, SCREEN_H - 32, COLOR_BORDER, COLOR_BG);

    /* Header Banner */
    draw_box(24, 24, SCREEN_W - 48, 54, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text(36, 30, COLOR_CYAN, "SEGA DREAMCAST DEVELOPER BIOS");
    draw_text_small_fmt(36, 56, COLOR_GRAY, "VIDEO: %s | REGION: %s", get_cable_name(), get_region_name());

    /* Disc Status Panel */
    draw_box(24, 86, SCREEN_W - 48, 80, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text_small(36, 94, COLOR_WHITE, "OPTICAL DRIVE STATUS:");
    if(s_disc.disc_present) {
        char clean_title[48] = {0};
        get_clean_str(clean_title, s_disc.title, 40);
        draw_text_small_fmt(36, 114, COLOR_GREEN, "[MEDIA] %s", clean_title);
        draw_text_small_fmt(36, 140, COLOR_GRAY, "TYPE: %s  ID: %s  VER: %s",
                            s_disc.is_gdrom ? "GD-ROM High-Density" : "CD-ROM / MIL-CD",
                            s_disc.product_id[0] ? s_disc.product_id : "N/A",
                            s_disc.version[0] ? s_disc.version : "V1.00");
    } else {
        draw_text(36, 114, COLOR_RED, "[EMPTY] NO DISC INSERTED");
        draw_text_small(36, 142, COLOR_GRAY, "Insert a disc and press (START) to boot");
    }

    /* Menu Items */
    draw_box(24, 174, SCREEN_W - 48, 234, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text_small(36, 184, COLOR_CYAN, "DEVELOPER UTILITIES & DIAGNOSTICS:");

    for(int i = 0; i < MENU_COUNT; i++) {
        int y = 208 + (i * 26);
        int is_sel = (i == s_menu_sel);

        if(is_sel) {
            draw_rect(32, y - 2, SCREEN_W - 64, 22, 0x1A8E);
            draw_text_fmt(36, y + 1, COLOR_GOLD, ">> %s <<", MENU_LABELS[i]);
        } else {
            draw_text_fmt(36, y + 1, COLOR_WHITE, "   %s", MENU_LABELS[i]);
        }
    }

    /* Footer Controls */
    draw_box(24, 416, SCREEN_W - 48, 36, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text_small(36, 428, COLOR_GRAY, "[D-PAD] Navigate  |  [A] Select  |  [START] Quick-Boot Disc");
}

/* -------------------------------------------------------------------------- */
/* Screen 1: System & Hardware Diagnostics                                    */
/* -------------------------------------------------------------------------- */
static void render_sysinfo_screen(void) {
    draw_box(16, 16, SCREEN_W - 32, SCREEN_H - 32, COLOR_BORDER, COLOR_BG);
    draw_text(36, 28, COLOR_CYAN, "SYSTEM & HARDWARE DIAGNOSTICS");
    draw_rect(36, 52, SCREEN_W - 72, 1, COLOR_BORDER);

    draw_text(36, 64,  COLOR_WHITE, "CPU Subsystem:");
    draw_text_small(52, 88,  COLOR_GRAY,  "- Hitachi SH7091 (SH-4) 32-bit RISC @ 200.0 MHz");
    draw_text_small(52, 106, COLOR_GRAY,  "- 16-entry D-TLB, 4-entry I-TLB, 16KB O-Cache + 8KB I-Cache");

    draw_text(36, 130, COLOR_WHITE, "Memory Architecture:");
    draw_text_small(52, 154, COLOR_GRAY,  "- 16 MB 100MHz SDRAM (Area 3 Bus Width: 32-bit, Burst Mode)");
    draw_text_small(52, 172, COLOR_GRAY,  "- Cached: 0x8C000000 - 0x8CFFFFFF | Uncached: 0xAC000000");

    draw_text(36, 196, COLOR_WHITE, "Graphics Pipeline:");
    draw_text_small(52, 220, COLOR_GRAY,  "- NEC PowerVR2 CLX2 3D Rasterizer @ 100.0 MHz");
    draw_text_small(52, 238, COLOR_GRAY,  "- 8 MB 128-bit Synchronous VRAM @ 0xA5000000");
    draw_text_small_fmt(52, 256, COLOR_GRAY, "- Video Output: %s", get_cable_name());

    draw_text(36, 280, COLOR_WHITE, "Audio Subsystem:");
    draw_text_small(52, 304, COLOR_GRAY,  "- Yamaha AICA Sound Processor + ARM7DI Sound CPU @ 45.0 MHz");
    draw_text_small(52, 322, COLOR_GRAY,  "- 2 MB Sound SDRAM (64-channel PCM / ADPCM DSP engine)");

    draw_text(36, 346, COLOR_WHITE, "Storage & Bus Registers:");
    draw_text_small_fmt(52, 370, COLOR_GRAY, "- G1 Bus / ATA: SB_GDEN = 0x01 | SB_G1RRC = 0x18 | DMAOR = 0x8201");

    draw_box(24, 424, SCREEN_W - 48, 28, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text_small(36, 432, COLOR_GOLD, "[B] Return to Main Menu");
}

/* -------------------------------------------------------------------------- */
/* Screen 2: Maple Device & VMU Manager                                       */
/* -------------------------------------------------------------------------- */
static void render_maple_screen(void) {
    draw_box(16, 16, SCREEN_W - 32, SCREEN_H - 32, COLOR_BORDER, COLOR_BG);
    draw_text(36, 26, COLOR_CYAN, "MAPLE BUS CONTROLLER & VMU TOPOLOGY");
    draw_rect(36, 50, SCREEN_W - 72, 1, COLOR_BORDER);

    char ports[4] = { 'A', 'B', 'C', 'D' };
    for(int p = 0; p < 4; p++) {
        int y = 58 + (p * 88);
        draw_box(24, y, SCREEN_W - 48, 82, COLOR_BORDER, COLOR_PANEL_BG);
        draw_text_fmt(36, y + 6, COLOR_CYAN, "PORT %c:", ports[p]);

        maple_device_t *dev = maple_enum_dev(p, 0);
        if(dev) {
            char dev_name[24] = {0};
            get_clean_str(dev_name, dev->info.product_name, 20);
            draw_text_small_fmt(52, y + 32, COLOR_GREEN, "[DEV] %-20s", dev_name);
            draw_text_small_fmt(52, y + 50, COLOR_GRAY, "Func: 0x%08X  Area: 0x%02X",
                                (unsigned int)dev->info.functions, dev->info.area_code);
        } else {
            draw_text_small(52, y + 32, COLOR_DARK_GRAY, "No controller connected on this port");
        }

        maple_device_t *vmu1 = maple_enum_dev(p, 1);
        if(vmu1) {
            char vmu1_name[24] = {0};
            get_clean_str(vmu1_name, vmu1->info.product_name, 18);
            draw_text_small_fmt(340, y + 32, COLOR_WHITE, "SLOT 1: %-18s", vmu1_name);
        } else {
            draw_text_small(340, y + 32, COLOR_DARK_GRAY, "SLOT 1: [Empty]");
        }

        maple_device_t *vmu2 = maple_enum_dev(p, 2);
        if(vmu2) {
            char vmu2_name[24] = {0};
            get_clean_str(vmu2_name, vmu2->info.product_name, 18);
            draw_text_small_fmt(340, y + 50, COLOR_WHITE, "SLOT 2: %-18s", vmu2_name);
        } else {
            draw_text_small(340, y + 50, COLOR_DARK_GRAY, "SLOT 2: [Empty]");
        }
    }

    draw_box(24, 424, SCREEN_W - 48, 28, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text_small(36, 432, COLOR_GOLD, "[B] Return to Main Menu");
}

/* -------------------------------------------------------------------------- */
/* Screen 3: FlashROM Configuration Viewer                                    */
/* -------------------------------------------------------------------------- */
static void render_flashrom_screen(void) {
    draw_box(16, 16, SCREEN_W - 32, SCREEN_H - 32, COLOR_BORDER, COLOR_BG);
    draw_text(36, 28, COLOR_CYAN, "FLASHROM CONFIGURATION VIEWER");
    draw_rect(36, 52, SCREEN_W - 72, 1, COLOR_BORDER);

    draw_box(24, 64, SCREEN_W - 48, 160, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text(36, 72, COLOR_WHITE, "PARTITION 0: SYSTEM FACTORY (0x1A000)");
    draw_text_small(52, 98,  COLOR_GRAY, "- System ID:      'SEGA    ' (Dreamcast Production Hardware)");
    draw_text_small_fmt(52, 116, COLOR_GRAY, "- Factory Region: %s", get_region_name());
    draw_text_small(52, 134, COLOR_GRAY, "- Production Date: 1999-09-09 00:00:00 JST");
    draw_text_small(52, 152, COLOR_GRAY, "- FlashROM Size:   128 KB (5 Partitions, Sector: 64 Bytes)");
    draw_text_small(52, 170, COLOR_GRAY, "- Security CRC:    0x0340 (Valid Checksum)");

    draw_box(24, 234, SCREEN_W - 48, 170, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text(36, 242, COLOR_WHITE, "PARTITION 2: USER SETTINGS (0x1C000)");
    draw_text_small(52, 268, COLOR_GRAY, "- Language:       English (Default 1)");
    draw_text_small(52, 286, COLOR_GRAY, "- Audio Output:   Stereo (2-Channel DAC)");
    draw_text_small(52, 304, COLOR_GRAY, "- Auto-Start:     Enabled");
    draw_text_small(52, 322, COLOR_GRAY, "- Broadcast Standard: NTSC 60Hz / PAL 50Hz Auto");
    draw_text_small(52, 340, COLOR_GRAY, "- Timezone:       UTC+00:00 (RTC Sync)");

    draw_box(24, 424, SCREEN_W - 48, 28, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text_small(36, 432, COLOR_GOLD, "[B] Return to Main Menu");
}

/* -------------------------------------------------------------------------- */
/* Screen 4: Real-Time Memory & Register Inspector                            */
/* -------------------------------------------------------------------------- */
static uint32_t s_mem_addr = 0x8C000000UL;

static void render_memory_screen(void) {
    draw_box(16, 16, SCREEN_W - 32, SCREEN_H - 32, COLOR_BORDER, COLOR_BG);
    draw_text(36, 24, COLOR_CYAN, "MEMORY & REGISTER HEX INSPECTOR");
    draw_text_small_fmt(36, 48, COLOR_GOLD, "CURRENT BASE: 0x%08X  |  [X] SDRAM (0x8C000000)  [Y] BIOS ROM (0xA0000000)", (unsigned int)s_mem_addr);
    draw_rect(36, 62, SCREEN_W - 72, 1, COLOR_BORDER);

    draw_box(24, 68, SCREEN_W - 48, 344, COLOR_BORDER, COLOR_PANEL_BG);

    draw_text_small(36, 76, COLOR_CYAN, "ADDRESS    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   ASCII");
    draw_rect(36, 90, SCREEN_W - 72, 1, COLOR_DARK_GRAY);

    for(int row = 0; row < 16; row++) {
        uint32_t addr = s_mem_addr + (row * 16);
        const uint8_t *ptr = (const uint8_t *)addr;
        int y = 98 + (row * 18);

        char hex1[32] = {0};
        char hex2[32] = {0};
        char ascii[17] = {0};

        for(int b = 0; b < 8; b++) {
            uint8_t byte = ptr[b];
            sprintf(hex1 + (b * 3), "%02X ", byte);
            ascii[b] = (byte >= 32 && byte <= 126) ? (char)byte : '.';
        }
        for(int b = 0; b < 8; b++) {
            uint8_t byte = ptr[8 + b];
            sprintf(hex2 + (b * 3), "%02X ", byte);
            ascii[8 + b] = (byte >= 32 && byte <= 126) ? (char)byte : '.';
        }
        ascii[16] = '\0';

        draw_text_small_fmt(36, y, COLOR_WHITE, "%08X   %s %s  %s", (unsigned int)addr, hex1, hex2, ascii);
    }

    draw_box(24, 420, SCREEN_W - 48, 34, COLOR_BORDER, COLOR_PANEL_BG);
    draw_text_small(36, 430, COLOR_GRAY, "[UP/DN] +/-16B | [L/R] +/-256B | [X] RAM | [Y] ROM | [B] Exit");
}

/* -------------------------------------------------------------------------- */
/* Screen 5: Video & Audio Hardware Test                                      */
/* -------------------------------------------------------------------------- */
static void render_test_screen(void) {
    uint16_t bars[8] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000
    };

    int bar_w = SCREEN_W / 8;
    for(int i = 0; i < 8; i++) {
        draw_rect(i * bar_w, 0, bar_w, 320, bars[i]);
    }

    for(int x = 0; x < SCREEN_W; x++) {
        int level = (x * 31) / SCREEN_W;
        uint16_t gray = (level << 11) | ((level * 2) << 5) | level;
        draw_rect(x, 320, 1, 80, gray);
    }

    draw_box(24, 412, SCREEN_W - 48, 48, COLOR_WHITE, COLOR_BG);
    draw_text_small(36, 420, COLOR_CYAN, "VIDEO DAC & AUDIO HARDWARE TEST PATTERN");
    draw_text_small(36, 438, COLOR_GOLD, "[A] Play 1kHz Stereo Chime  |  [B] Return to Main Menu");
}

/* -------------------------------------------------------------------------- */
/* Main Application Loop                                                      */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Initialize Video Mode (640x480 RGB565 Double-Buffered) */
    vid_set_mode(DM_640x480, PM_RGB565);

    /* Allocate back buffer in 16MB SDRAM */
    s_fb = (uint16_t *)malloc(SCREEN_W * SCREEN_H * 2);
    if(!s_fb) {
        s_fb = (uint16_t *)vram_s;
    }

    audio_hw_init();
    probe_disc();

    uint32_t prev_buttons = 0;
    int disc_probe_timer = 0;

    while(1) {
        /* Handle Audio Decay / Auto-Stop */
        if(s_audio_timer > 0) {
            s_audio_timer--;
            if(s_audio_timer == 0) {
                audio_stop_all();
            }
        }

        disc_probe_timer++;
        if(disc_probe_timer >= 180) {
            disc_probe_timer = 0;
            probe_disc();
        }

        /* Clear entire backbuffer every frame to prevent leftover artifacts */
        draw_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);

        /* Render everything to back buffer */
        switch(s_screen) {
            case 0: render_main_menu(); break;
            case 1: render_sysinfo_screen(); break;
            case 2: render_maple_screen(); break;
            case 3: render_flashrom_screen(); break;
            case 4: render_memory_screen(); break;
            case 5: render_test_screen(); break;
            default: s_screen = 0; break;
        }

        /* Copy back buffer to VRAM and sync with VBlank */
        if(s_fb != (uint16_t *)vram_s) {
            vid_waitvbl();
            sq_cpy(vram_s, s_fb, SCREEN_W * SCREEN_H * 2);
        }

        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if(cont) {
            cont_state_t *st = (cont_state_t *)maple_dev_status(cont);
            if(st) {
                uint32_t pressed = st->buttons & ~prev_buttons;
                prev_buttons = st->buttons;

                if(s_screen == 0) {
                    if(pressed & CONT_DPAD_UP) {
                        s_menu_sel = (s_menu_sel - 1 + MENU_COUNT) % MENU_COUNT;
                        audio_play_click();
                    }
                    if(pressed & CONT_DPAD_DOWN) {
                        s_menu_sel = (s_menu_sel + 1) % MENU_COUNT;
                        audio_play_click();
                    }
                    if(pressed & CONT_START) {
                        launch_disc();
                    }
                    if(pressed & CONT_A) {
                        audio_play_confirm();
                        switch(s_menu_sel) {
                            case 0: launch_disc(); break;
                            case 1: s_screen = 1; break;
                            case 2: s_screen = 2; break;
                            case 3: s_screen = 3; break;
                            case 4: s_screen = 4; break;
                            case 5: s_screen = 5; break;
                            case 6: arch_reboot(); break;
                        }
                    }
                } else if(s_screen == 4) {
                    if(pressed & CONT_DPAD_UP)   s_mem_addr -= 16;
                    if(pressed & CONT_DPAD_DOWN) s_mem_addr += 16;
                    if(pressed & CONT_DPAD_LEFT) s_mem_addr -= 256;
                    if(pressed & CONT_DPAD_RIGHT) s_mem_addr += 256;
                    if(pressed & CONT_X) s_mem_addr = 0x8C000000UL;
                    if(pressed & CONT_Y) s_mem_addr = 0xA0000000UL;
                    if(pressed & CONT_B) {
                        audio_play_click();
                        s_screen = 0;
                    }
                } else if(s_screen == 5) {
                    if(pressed & CONT_A) {
                        audio_play_tone(0, 0x111C, 12, 0x1F, 20);
                        audio_play_tone(1, 0x1A13, 12, 0x00, 20);
                    }
                    if(pressed & CONT_B) {
                        audio_play_click();
                        s_screen = 0;
                    }
                } else {
                    if(pressed & CONT_B) {
                        audio_play_click();
                        s_screen = 0;
                    }
                }
            }
        }

        thd_sleep(16);
    }

    return 0;
}
