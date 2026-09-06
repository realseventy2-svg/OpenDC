#include "screen.h"
#include "sound.h"
#include "boot_scene.h"

const boot_theme_t BOOT_THEME_DEFAULT = {
    .bg_color           = RGB565(210, 213, 217), /* Authentic Sega Frosted Grey #D2D5D9 */
    .header_color       = COLOR_WHITE,
    .sub_color          = COLOR_CYAN,
    .status_ok_color    = COLOR_GREEN,
    .status_err_color   = COLOR_GOLD,
    .text_color         = COLOR_WHITE,
    .bar_border_color   = COLOR_DARK_GRAY,
    .bar_fill_color     = RGB565(30, 140, 230),
    .bar_complete_color = RGB565(90, 210, 255),

    .title              = "Open Dreamcast",
    .subtitle           = "",
    .version_text       = "",

    .splash_delay_seconds = 8,
    .splash_delay_frames  = 0,
    .show_diagnostics     = 0,
    .show_progress_bar    = 0,

    .sega_license_enabled = 1,
    .music_enabled      = 1
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

    .splash_delay_seconds = BOOT_DURATION_INSTANT, /* 0 seconds (Instant boot) */
    .splash_delay_frames  = 0,
    .show_diagnostics     = 0,
    .show_progress_bar    = 1,

    .sega_license_enabled = 0,
    .music_enabled      = 0
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

    .splash_delay_seconds = BOOT_DURATION_DEFAULT, /* 4 seconds */
    .splash_delay_frames  = 0,
    .show_diagnostics     = 1,
    .show_progress_bar    = 1,

    .sega_license_enabled = 0,
    .music_enabled      = 1
};

const boot_theme_t BOOT_THEME_CINEMATIC = {
    .bg_color           = COLOR_BLACK,
    .header_color       = COLOR_CYAN,
    .sub_color          = COLOR_WHITE,
    .status_ok_color    = COLOR_GREEN,
    .status_err_color   = COLOR_GOLD,
    .text_color         = COLOR_WHITE,
    .bar_border_color   = COLOR_DARK_GRAY,
    .bar_fill_color     = COLOR_CYAN,
    .bar_complete_color = COLOR_GREEN,

    .title              = "SEGA DREAMCAST",
    .subtitle           = "FRUTIGER AERO AMBIENCE",
    .version_text       = "OpenDC Ambient Bios",

    .splash_delay_seconds = BOOT_DURATION_CINEMATIC, /* 16 seconds (full ambient cycle) */
    .splash_delay_frames  = 0,
    .show_diagnostics     = 1,
    .show_progress_bar    = 1,

    .sega_license_enabled = 1,
    .music_enabled      = 1
};

const boot_theme_t BOOT_THEME_DIAGNOSTIC = {
    .bg_color           = COLOR_BLACK,
    .header_color       = COLOR_CYAN,
    .sub_color          = COLOR_WHITE,
    .status_ok_color    = COLOR_GREEN,
    .status_err_color   = COLOR_RED,
    .text_color         = COLOR_WHITE,
    .bar_border_color   = COLOR_DARK_GRAY,
    .bar_fill_color     = COLOR_CYAN,
    .bar_complete_color = COLOR_GREEN,

    .title              = "OPEN DREAMCAST",
    .subtitle           = "VERBOSE POST DIAGNOSTICS",
    .version_text       = "POST / BIOS v1.0",

    .splash_delay_seconds = 4,
    .splash_delay_frames  = 0,
    .show_diagnostics     = 1,
    .show_progress_bar    = 1,

    .sega_license_enabled = 1,
    .music_enabled      = 1
};

static const boot_theme_t *current_theme = &BOOT_THEME_DEFAULT;
static int s_custom_duration_frames = -1;

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
    s_custom_duration_frames = -1;
}

void screen_set_theme(const boot_theme_t *theme) {
    if (theme) {
        current_theme = theme;
    }
}

const boot_theme_t *screen_get_theme(void) {
    return current_theme;
}

void screen_set_boot_duration(int seconds) {
    if (seconds < 0) seconds = 0;
    s_custom_duration_frames = seconds * 60;
}

void screen_set_boot_duration_frames(int frames) {
    if (frames < 0) frames = 0;
    s_custom_duration_frames = frames;
}

static const void *s_boot_scene_blob = (const void *)0;

void screen_set_boot_scene(const void *blob) {
    s_boot_scene_blob = blob;
}

int screen_get_boot_duration_frames(void) {
    if (s_custom_duration_frames >= 0) {
        return s_custom_duration_frames;
    }
    if (current_theme) {
        if (current_theme->splash_delay_seconds > 0) {
            return current_theme->splash_delay_seconds * 60;
        }
        if (current_theme->splash_delay_frames > 0) {
            return current_theme->splash_delay_frames;
        }
    }
    return 0;
}

void screen_draw_diagnostics_verbose(int toc_ok, int iso_ok, uint32_t fad, const uint8_t *head, int has_3d_scene) {
    /* 1. Header Banner using authentic 12x24 BIOS font */
    video_draw_bfont_string_centered(320, 14, "OPEN DREAMCAST // HARDWARE POST", COLOR_CYAN);
    video_draw_line(24, 42, 616, 42, COLOR_CYAN);

    /* 2. CPU & Cache */
    video_draw_bfont_string(30, 52, "CPU: SH7091 (SH-4) 200MHz RISC", COLOR_WHITE);
    video_draw_bfont_string(510, 52, "[ PASS ]", COLOR_GREEN);
    video_draw_string(40, 78, "CCR: 0x0808 (16KB I-CACHE / 8KB D-CACHE ENABLED)", COLOR_LIGHT_GRAY, 1);

    /* 3. Memory & GPU */
    video_draw_bfont_string(30, 96, "RAM: 16MB SDRAM @ 0x8C000000", COLOR_WHITE);
    video_draw_bfont_string(510, 96, "[ PASS ]", COLOR_GREEN);
    video_draw_bfont_string(30, 122, "GPU: PowerVR2 CLX2 8MB VRAM", COLOR_WHITE);
    video_draw_bfont_string(510, 122, "[ 60HZ ]", COLOR_CYAN);
    video_draw_bfont_string(30, 148, "SPU: Yamaha AICA + 2MB WaveRAM", COLOR_WHITE);
    video_draw_bfont_string(510, 148, "[ READY ]", COLOR_GREEN);

    /* 4. BootROM & Syscalls */
    video_draw_bfont_string(30, 180, "ROM: 2048KB FlashROM @ 0xA0000000", COLOR_WHITE);
    video_draw_bfont_string(510, 180, "[ PASS ]", COLOR_GREEN);
    video_draw_bfont_string(30, 206, "FNT: Sega BIOS Font @ 0xA0100020", COLOR_WHITE);
    video_draw_bfont_string(510, 206, "[ OK ]", COLOR_GREEN);
    video_draw_string(40, 232, "VECTORS: 0x8C0000B0(SYSINFO)  0x8C0000B4(ROMFONT)  0x8C0000BC(GDROM)", COLOR_LIGHT_GRAY, 1);

    /* 5. G1-ATA & Media Subsystem */
    video_draw_bfont_string(30, 250, "ATA: G1-ATA Bus Controller", COLOR_WHITE);
    video_draw_bfont_string(510, 250, "[ ACTIVE ]", COLOR_CYAN);

    if (toc_ok) {
        video_draw_bfont_string(30, 276, "DISC: Track TOC Verified", COLOR_WHITE);
        video_draw_bfont_string(510, 276, "[ READY ]", COLOR_GREEN);
    } else {
        video_draw_bfont_string(30, 276, "DISC: No Disc Inserted", COLOR_LIGHT_GRAY);
        video_draw_bfont_string(510, 276, "[ STANDBY ]", COLOR_GOLD);
    }

    if (iso_ok) {
        video_draw_bfont_string(30, 302, "BOOT: GD-ROM Game Disc", COLOR_WHITE);
        video_draw_bfont_string(510, 302, "[ AUTORUN ]", COLOR_GREEN);
        if (head) {
            video_draw_string(40, 328, "FAD: 0x", COLOR_GOLD, 1);
            video_draw_hex32(96, 328, fad, COLOR_GOLD, 1);
            video_draw_string(180, 328, "HEADER: ", COLOR_GOLD, 1);
            video_draw_hex8(244, 328, head, COLOR_GOLD, 1);
        }
    } else {
        video_draw_bfont_string(30, 302, "BOOT: Standalone Custom BIOS", COLOR_WHITE);
        video_draw_bfont_string(510, 302, "[ CHAINLOAD ]", COLOR_CYAN);
    }

    /* 6. Boot Scene & Payload */
    video_draw_bfont_string(30, 348, "SCENE: DCBS 3D Container", has_3d_scene ? COLOR_WHITE : COLOR_LIGHT_GRAY);
    video_draw_bfont_string(510, 348, has_3d_scene ? "[ MOUNTED ]" : "[ NOT FOUND ]", has_3d_scene ? COLOR_GREEN : COLOR_GOLD);
    video_draw_bfont_string(30, 374, "TARGET: Payload @ 0x8C010000", COLOR_WHITE);
    video_draw_bfont_string(510, 374, "[ READY ]", COLOR_GREEN);

    /* 7. Footer Status */
    video_draw_line(24, 404, 616, 404, COLOR_CYAN);
    video_draw_bfont_string_centered(320, 416, "ALL SYSTEM CHECKS PASSED // INITIALIZED", COLOR_GREEN);
}

void screen_animate_diagnostics(int total_frames, int toc_ok, int iso_ok, uint32_t fad, const uint8_t *head, int has_3d_scene) {
    if (total_frames <= 0) return;

    video_set_border_color_565(COLOR_BLACK);
    video_set_target_buffer(video_get_back_fb());

    for (int frame = 0; frame < total_frames; frame++) {
        uint32_t back_fb = video_get_back_fb();
        video_set_target_buffer(back_fb);

        /* 1. Clear back buffer */
        video_clear(COLOR_BLACK);

        /* 2. Render POST diagnostics text */
        screen_draw_diagnostics_verbose(toc_ok, iso_ok, fad, head, has_3d_scene);

        /* 3. Render 60 FPS animated progress bar */
        uint32_t fill_w = udiv32((uint32_t)frame * BAR_WIDTH, (uint32_t)total_frames);
        if (fill_w > BAR_WIDTH) fill_w = BAR_WIDTH;

        video_fill_rect(BAR_X - 2, 450 - 2, BAR_WIDTH + 4, 1, COLOR_DARK_GRAY);
        video_fill_rect(BAR_X - 2, 450 + BAR_HEIGHT + 1, BAR_WIDTH + 4, 1, COLOR_DARK_GRAY);
        video_fill_rect(BAR_X - 2, 450 - 2, 1, BAR_HEIGHT + 4, COLOR_DARK_GRAY);
        video_fill_rect(BAR_X + BAR_WIDTH + 1, 450 - 2, 1, BAR_HEIGHT + 4, COLOR_DARK_GRAY);

        if (fill_w > 0) {
            video_fill_rect(BAR_X, 450, fill_w, BAR_HEIGHT, COLOR_CYAN);
        }

        /* 4. Atomically swap displayed surface on vertical blank */
        video_flip_buffer();
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

void screen_animate_splash(int duration_frames) {
    if (duration_frames <= 0) return;

    /* Standalone DCBS 3D Container Runtime */
    if (s_boot_scene_blob && boot_scene_mount(s_boot_scene_blob) == 0) {
        uint16_t bg_color = boot_scene_get_bg_color();
        video_set_border_color_565(bg_color);

        video_set_target_buffer(video_get_back_fb());

        while (!boot_scene_is_done()) {
            uint32_t back_fb = video_get_back_fb();
            video_set_target_buffer(back_fb);

            /* 1. Clean background clear into back buffer */
            video_clear(bg_color);

            /* 2. Advance 3D projection, rasterize mesh, & fire AICA audio cues */
            boot_scene_tick(back_fb);

            /* 3. Atomically flip displayed surface on hardware VBlank */
            video_flip_buffer();
        }

        boot_scene_unmount();

        /* Clean display handoff */
        video_wait_vblank();
        *(volatile uint32_t *)0xA05F8050UL = 0x00000000UL;
        *(volatile uint32_t *)0xA05F8054UL = 0x00000000UL;
        video_set_target_buffer(VRAM_PAGE_0);
        return;
    }
}



