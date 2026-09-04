#include "screen.h"

const boot_theme_t BOOT_THEME_DEFAULT = {
    .bg_color           = COLOR_BLACK,
    .header_color       = COLOR_GREEN,
    .sub_color          = COLOR_WHITE,
    .status_ok_color    = COLOR_GREEN,
    .status_err_color   = COLOR_GOLD,
    .text_color         = COLOR_WHITE,
    .bar_border_color   = COLOR_DARK_GRAY,
    .bar_fill_color     = COLOR_GREEN,
    .bar_complete_color = COLOR_CYAN,

    .title              = "SEGA DREAMCAST",
    .subtitle           = "CUSTOM BOOT ROM",
    .version_text       = "OpenDC v1.0",

    .splash_delay_seconds = 4,
    .show_diagnostics     = 1,
    .show_progress_bar    = 1,

    .cube_enabled       = 1,
    .cube_center_x      = 320,
    .cube_center_y      = 290,
    .cube_size          = 40,
    .cube_color         = COLOR_CYAN,
    .sega_license_enabled = 0
};

const boot_theme_t BOOT_THEME_MINIMAL = {
    .bg_color           = COLOR_BLACK,
    .header_color       = COLOR_WHITE,
    .sub_color          = COLOR_LIGHT_GRAY,
    .status_ok_color    = COLOR_CYAN,
    .status_err_color   = COLOR_RED,
    .text_color         = COLOR_WHITE,
    .bar_border_color   = COLOR_DARK_GRAY,
    .bar_fill_color     = COLOR_CYAN,
    .bar_complete_color = COLOR_WHITE,

    .title              = "DREAMCAST",
    .subtitle           = "FAST BOOT",
    .version_text       = NULL,

    .splash_delay_seconds = 0,
    .show_diagnostics     = 0,
    .show_progress_bar    = 1,

    .cube_enabled       = 0,
    .cube_center_x      = 320,
    .cube_center_y      = 290,
    .cube_size          = 40,
    .cube_color         = COLOR_WHITE,
    .sega_license_enabled = 0
};

const boot_theme_t BOOT_THEME_DARK = {
    .bg_color           = COLOR_BLACK,
    .header_color       = COLOR_CYAN,
    .sub_color          = COLOR_GOLD,
    .status_ok_color    = COLOR_CYAN,
    .status_err_color   = COLOR_RED,
    .text_color         = COLOR_LIGHT_GRAY,
    .bar_border_color   = COLOR_DARK_GRAY,
    .bar_fill_color     = COLOR_CYAN,
    .bar_complete_color = COLOR_GOLD,

    .title              = "SEGA DREAMCAST",
    .subtitle           = "KALLISTIOS FIRMWARE",
    .version_text       = "OpenDC Custom BIOS",

    .splash_delay_seconds = 4,
    .show_diagnostics     = 1,
    .show_progress_bar    = 1,

    .cube_enabled       = 1,
    .cube_center_x      = 320,
    .cube_center_y      = 290,
    .cube_size          = 40,
    .cube_color         = COLOR_GOLD,
    .sega_license_enabled = 0
};

static const boot_theme_t *current_theme = &BOOT_THEME_DEFAULT;

#define BAR_X       120
#define BAR_Y       466
#define BAR_WIDTH   400
#define BAR_HEIGHT  8

static uint32_t udiv32(uint32_t num, uint32_t den) {
    if (den == 0) return 0;
    uint32_t quot = 0, qbit = 1;
    while ((int32_t)den >= 0 && den < num) {
        den <<= 1;
        qbit <<= 1;
    }
    while (qbit) {
        if (num >= den) {
            num -= den;
            quot |= qbit;
        }
        den >>= 1;
        qbit >>= 1;
    }
    return quot;
}

static int32_t sdiv32(int32_t num, int32_t den) {
    if (den == 0) return 0;
    int sign = 1;
    uint32_t unum, uden;
    if (num < 0) {
        sign = -sign;
        unum = (uint32_t)-num;
    } else {
        unum = (uint32_t)num;
    }
    if (den < 0) {
        sign = -sign;
        uden = (uint32_t)-den;
    } else {
        uden = (uint32_t)den;
    }
    uint32_t quot = udiv32(unum, uden);
    return (sign < 0) ? -(int32_t)quot : (int32_t)quot;
}

/* 8.8 Fixed-Point Sine Quarter-Wave Table (0 to 90 degrees in 64 steps, 256 = 1.0) */
static const int16_t sin_quarter[65] = {
    0,   6,  12,  18,  25,  31,  37,  43,  49,  56,  62,  68,  74,  80,  86,  92,
   97, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176,
  181, 185, 189, 193, 197, 201, 205, 208, 212, 215, 219, 222, 225, 228, 231, 233,
  236, 238, 240, 242, 244, 246, 247, 249, 250, 251, 252, 253, 254, 254, 255, 255,
  256
};

static int32_t sin_fixed(int angle) {
    angle &= 0xFF;
    if (angle <= 64) return sin_quarter[angle];
    if (angle <= 128) return sin_quarter[128 - angle];
    if (angle <= 192) return -sin_quarter[angle - 128];
    return -sin_quarter[256 - angle];
}

static int32_t cos_fixed(int angle) {
    return sin_fixed(angle + 64);
}

/* 8 Vertices of a 3D unit cube */
static const int8_t cube_verts[8][3] = {
    { -1, -1, -1 }, /* 0 */
    {  1, -1, -1 }, /* 1 */
    {  1,  1, -1 }, /* 2 */
    { -1,  1, -1 }, /* 3 */
    { -1, -1,  1 }, /* 4 */
    {  1, -1,  1 }, /* 5 */
    {  1,  1,  1 }, /* 6 */
    { -1,  1,  1 }  /* 7 */
};

/* 12 Edges connecting cube vertices */
static const uint8_t cube_edges[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, /* Back face */
    {4, 5}, {5, 6}, {6, 7}, {7, 4}, /* Front face */
    {0, 4}, {1, 5}, {2, 6}, {3, 7}  /* Connecting edges */
};

void screen_init(const boot_theme_t *theme) {
    if (theme) {
        current_theme = theme;
    } else {
        current_theme = &BOOT_THEME_DEFAULT;
    }
}

void screen_set_theme(const boot_theme_t *theme) {
    if (theme) {
        current_theme = theme;
    }
}

const boot_theme_t *screen_get_theme(void) {
    return current_theme;
}

void screen_draw_splash(void) {
    video_clear(current_theme->bg_color);

    if (current_theme->title) {
        video_draw_string_centered(320, 70, current_theme->title, current_theme->header_color, 3);
    }
    if (current_theme->subtitle) {
        video_draw_string_centered(320, 130, current_theme->subtitle, current_theme->sub_color, 4);
    }
    if (current_theme->version_text) {
        video_draw_string_centered(320, 190, current_theme->version_text, current_theme->text_color, 2);
    }

    if (current_theme->show_progress_bar) {
        /* Draw progress bar outline/box */
        video_fill_rect(BAR_X - 2, BAR_Y - 2, BAR_WIDTH + 4, 1, current_theme->bar_border_color);
        video_fill_rect(BAR_X - 2, BAR_Y + BAR_HEIGHT + 1, BAR_WIDTH + 4, 1, current_theme->bar_border_color);
        video_fill_rect(BAR_X - 2, BAR_Y - 2, 1, BAR_HEIGHT + 4, current_theme->bar_border_color);
        video_fill_rect(BAR_X + BAR_WIDTH + 1, BAR_Y - 2, 1, BAR_HEIGHT + 4, current_theme->bar_border_color);
    }
}

void screen_draw_disc_status(int toc_ok, int iso_ok, uint32_t fad, const uint8_t *head) {
    if (!current_theme->show_diagnostics) return;

    video_draw_string_centered(320, 385,
                               toc_ok ? "TOC OK" : "TOC ERROR",
                               toc_ok ? current_theme->status_ok_color : current_theme->status_err_color,
                               2);

    video_draw_string_centered(320, 412,
                               iso_ok ? "ISO OK" : "ISO ERROR",
                               iso_ok ? current_theme->status_ok_color : current_theme->status_err_color,
                               2);

    video_draw_string(240, 440, "FAD", current_theme->text_color, 1);
    video_draw_hex32(280, 440, fad, current_theme->text_color, 1);

    if (head) {
        video_draw_hex8(380, 440, head, current_theme->text_color, 1);
    }
}

void screen_update_progress(uint32_t current_sectors, uint32_t total_sectors) {
    if (!current_theme->show_progress_bar || total_sectors == 0) return;

    uint32_t fill_w = udiv32(current_sectors * BAR_WIDTH, total_sectors);
    if (fill_w > BAR_WIDTH) fill_w = BAR_WIDTH;

    if (fill_w > 0) {
        video_fill_rect(BAR_X, BAR_Y, fill_w, BAR_HEIGHT, current_theme->bar_fill_color);
    }
}

void screen_finish_progress(void) {
    if (!current_theme->show_progress_bar) return;
    video_fill_rect(BAR_X, BAR_Y, BAR_WIDTH, BAR_HEIGHT, current_theme->bar_complete_color);
}

void screen_show_fault(uint32_t pc, uint32_t expevt) {
    video_clear(COLOR_BLACK);
    video_draw_string_centered(320, 140, "FAULT", COLOR_GOLD, 5);

    video_draw_string_centered(220, 260, "AT", COLOR_WHITE, 3);
    video_draw_hex32(300, 260, pc, COLOR_WHITE, 3);

    video_draw_string_centered(220, 320, "CODE", COLOR_WHITE, 3);
    video_draw_hex32(340, 320, expevt, COLOR_WHITE, 3);
}

void screen_draw_cube(int cx, int cy, int size, int ax, int ay, int az, uint16_t color) {
    int32_t sin_y = sin_fixed(ay), cos_y = cos_fixed(ay);
    int32_t sin_x = sin_fixed(ax), cos_x = cos_fixed(ax);
    int32_t sin_z = sin_fixed(az), cos_z = cos_fixed(az);

    int proj_x[8];
    int proj_y[8];

    for (int i = 0; i < 8; i++) {
        int32_t x0 = (int32_t)cube_verts[i][0] * size;
        int32_t y0 = (int32_t)cube_verts[i][1] * size;
        int32_t z0 = (int32_t)cube_verts[i][2] * size;

        /* Yaw (Y-axis rotation) */
        int32_t x1 = (x0 * cos_y + z0 * sin_y) >> 8;
        int32_t z1 = (-x0 * sin_y + z0 * cos_y) >> 8;

        /* Pitch (X-axis rotation) */
        int32_t y2 = (y0 * cos_x - z1 * sin_x) >> 8;
        int32_t z2 = (y0 * sin_x + z1 * cos_x) >> 8;

        /* Roll (Z-axis rotation) */
        int32_t x3 = (x1 * cos_z - y2 * sin_z) >> 8;
        int32_t y3 = (x1 * sin_z + y2 * cos_z) >> 8;

        /* Perspective Projection */
        int32_t z_dist = z2 + 220;
        if (z_dist < 20) z_dist = 20;

        proj_x[i] = cx + (int)sdiv32(x3 * 200, z_dist);
        proj_y[i] = cy + (int)sdiv32(y3 * 200, z_dist);
    }

    /* Draw 12 cube edges */
    for (int e = 0; e < 12; e++) {
        int v0 = cube_edges[e][0];
        int v1 = cube_edges[e][1];
        video_draw_line(proj_x[v0], proj_y[v0], proj_x[v1], proj_y[v1], color);
    }
}

void screen_animate_splash(int duration_frames) {
    if (!current_theme->cube_enabled || duration_frames <= 0) {
        if (current_theme->splash_delay_seconds > 0) {
            video_wait_seconds(current_theme->splash_delay_seconds);
        }
        return;
    }

    int cx = current_theme->cube_center_x;
    int cy = current_theme->cube_center_y;
    int size = current_theme->cube_size;
    uint16_t color = current_theme->cube_color;
    uint16_t bg = current_theme->bg_color;

    /* Cube Bounding Box for Clearing */
    int clear_w = 200;
    int clear_h = 160;
    int clear_x = cx - (clear_w / 2);
    int clear_y = cy - (clear_h / 2);

    /* Clone static background, diagnostics, and text to back buffer */
    video_sync_buffers();
    video_set_target_buffer(video_get_back_fb());

    for (int frame = 0; frame < duration_frames; frame++) {
        /* Clear previous cube position on back buffer (invisible to user) */
        video_fill_rect(clear_x, clear_y, clear_w, clear_h, bg);

        /* Smooth 3-axis rotation */
        int ax = (frame * 2) & 0xFF;
        int ay = (frame * 3) & 0xFF;
        int az = (frame * 1) & 0xFF;

        /* Draw new cube into back buffer */
        screen_draw_cube(cx, cy, size, ax, ay, az, color);

        /* Hardware Page-Flip on VBlank with zero flicker */
        video_flip_buffer();
    }

    /* Synchronize final frame to both buffers and set Page 0 active for game boot */
    video_sync_buffers();
    video_set_target_buffer(VRAM_PAGE_0);
    *(volatile uint32_t *)0xA05F8050UL = 0x00000000UL;
}
