#ifndef OPENDC_BOOTLOADER_SCREEN_H
#define OPENDC_BOOTLOADER_SCREEN_H

#include "video.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint16_t bg_color;
    uint16_t header_color;
    uint16_t sub_color;
    uint16_t status_ok_color;
    uint16_t status_err_color;
    uint16_t text_color;
    uint16_t bar_border_color;
    uint16_t bar_fill_color;
    uint16_t bar_complete_color;

    const char *title;
    const char *subtitle;
    const char *version_text;

    int splash_delay_seconds;
    int show_diagnostics;
    int show_progress_bar;
} boot_theme_t;

/* Global default theme instance */
extern const boot_theme_t BOOT_THEME_DEFAULT;
extern const boot_theme_t BOOT_THEME_MINIMAL;
extern const boot_theme_t BOOT_THEME_DARK;

void screen_init(const boot_theme_t *theme);
void screen_set_theme(const boot_theme_t *theme);
const boot_theme_t *screen_get_theme(void);

void screen_draw_splash(void);
void screen_draw_disc_status(int toc_ok, int iso_ok, uint32_t fad, const uint8_t *head);
void screen_update_progress(uint32_t current_sectors, uint32_t total_sectors);
void screen_finish_progress(void);
void screen_show_fault(uint32_t pc, uint32_t expevt);

#endif /* OPENDC_BOOTLOADER_SCREEN_H */
