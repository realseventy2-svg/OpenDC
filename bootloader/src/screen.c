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

    .splash_delay_seconds = 1,
    .show_diagnostics     = 1,
    .show_progress_bar    = 1
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
    .show_progress_bar    = 1
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

    .splash_delay_seconds = 1,
    .show_diagnostics     = 1,
    .show_progress_bar    = 1
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
        video_draw_string_centered(320, 110, current_theme->title, current_theme->header_color, 3);
    }
    if (current_theme->subtitle) {
        video_draw_string_centered(320, 200, current_theme->subtitle, current_theme->sub_color, 4);
    }
    if (current_theme->version_text) {
        video_draw_string_centered(320, 270, current_theme->version_text, current_theme->text_color, 2);
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

    video_draw_string_centered(320, 380,
                               toc_ok ? "TOC OK" : "TOC ERROR",
                               toc_ok ? current_theme->status_ok_color : current_theme->status_err_color,
                               2);

    video_draw_string_centered(320, 410,
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
