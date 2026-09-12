#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/video.h>
#include <arch/exec.h>
#include <dc/sq.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "font.h"
#include "audio.h"
#include "disc.h"
#include "flashrom_ui.h"
#include "sysinfo_ui.h"
#include "maple_ui.h"
#include "memory_ui.h"
#include "test_ui.h"

extern const uint8_t romdisk[];

/* Fast boot flags */
KOS_INIT_FLAGS(INIT_IRQ | INIT_THD_PREEMPT | INIT_CONTROLLER | INIT_VMU | INIT_NO_DCLOAD);

/* Framebuffer pointer */
static uint16_t *s_fb = NULL;
static bios_screen_t s_screen = SCREEN_MAIN_MENU;
static int s_menu_sel = 0;

#define MENU_COUNT 7
static const char *MENU_ITEMS[MENU_COUNT] = {
    "Boot into GD-ROM Disc",
    "System & Hardware Diagnostics",
    "Maple Bus & VMU Manager",
    "FlashROM Configuration",
    "Memory & Register Hex Inspector",
    "Video & Audio Hardware Test",
    "Reboot Console"
};

/* -------------------------------------------------------------------------- */
/* Screen 0: Authentic Sega Developer Bootmenu                                */
/* -------------------------------------------------------------------------- */
static void render_main_menu(void) {
    int start_x = MARGIN_X;
    int y = 36;
    const disc_info_t *disc = disc_get_info();

    /* Title */
    draw_text_2x(start_x, y, COLOR_WHITE, "DREAMCAST BOOTMENU");
    y += 32;

    /* Menu Options with > cursor */
    for(int i = 0; i < MENU_COUNT; i++) {
        if(i == s_menu_sel) {
            draw_text_2x_fmt(start_x, y, COLOR_WHITE, "> %s", MENU_ITEMS[i]);
        } else {
            draw_text_2x_fmt(start_x, y, COLOR_LIGHT_GRAY, "  %s", MENU_ITEMS[i]);
        }
        y += 24;
    }

    y += 20;

    /* Developer status comments */
    char clean_title[48] = {0};
    get_clean_str(clean_title, disc->title, 36);

    if(disc->disc_present) {
        draw_text_fmt(start_x, y, COLOR_GRAY,
            ".// Disc: [%s] %s (%s)",
            disc->is_gdrom ? "GD-ROM" : "CD-ROM",
            clean_title,
            disc->product_id[0] ? disc->product_id : "HDR-XXXX");
    } else {
        draw_text(start_x, y, COLOR_GRAY, ".// Disc: No Disc Inserted in Drive [STANDBY]");
    }
    y += 18;

    draw_text(start_x, y, COLOR_GRAY,
        ".// Core: SH-4 200MHz | PVR2 100MHz | AICA 45MHz | 16MB SDRAM");
    y += 18;

    draw_text_fmt(start_x, y, COLOR_GRAY,
        ".// Output: %s | Region: %s | RTC Synced",
        get_cable_name(), get_region_name());
    y += 18;

    draw_text(start_x, y, COLOR_GRAY,
        ".// Status: System Initialized [ OK ]");

    /* Controls footer */
    draw_text(start_x, 436, COLOR_LIGHT_GRAY,
        "(A) Select   (START) Fast-Boot   (UP/DOWN) Navigate");
}

/* -------------------------------------------------------------------------- */
/* Main Application Entry & Event Loop                                        */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Disable KOS fb console debug prints to avoid any artifact on VRAM */
    dbgio_disable();

    /* Initialize Video Mode (640x480 RGB565 Double-Buffered) */
    vid_set_mode(DM_640x480, PM_RGB565);
    sq_set16(vram_s, 0, SCREEN_W * SCREEN_H * 2);

    /* Allocate back buffer in SDRAM */
    s_fb = (uint16_t *)malloc(SCREEN_W * SCREEN_H * 2);
    if(!s_fb) {
        s_fb = (uint16_t *)vram_s;
    }
    memset(s_fb, 0, SCREEN_W * SCREEN_H * 2);
    font_set_fb(s_fb);

    /* Initialize Subsystems */
    audio_hw_init();
    disc_init();
    flashrom_ui_init();

    uint32_t prev_buttons = 0;
    int disc_probe_timer = 0;

    while(1) {
        /* Update audio decay timer */
        audio_update();

        /* Periodically poll disc drive state */
        disc_probe_timer++;
        if(disc_probe_timer >= 180) {
            disc_probe_timer = 0;
            disc_probe();
        }

        /* Clear backbuffer to pure black */
        draw_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BLACK);

        /* Route to active screen renderer */
        switch(s_screen) {
            case SCREEN_MAIN_MENU: render_main_menu(); break;
            case SCREEN_SYSINFO:   sysinfo_ui_render(); break;
            case SCREEN_MAPLE:     maple_ui_render(); break;
            case SCREEN_FLASHROM:  flashrom_ui_render(); break;
            case SCREEN_MEMORY:    memory_ui_render(); break;
            case SCREEN_TEST:      test_ui_render(); break;
            default:               s_screen = SCREEN_MAIN_MENU; break;
        }

        /* Sync with VBlank and DMA transfer backbuffer to VRAM */
        if(s_fb != (uint16_t *)vram_s) {
            vid_waitvbl();
            sq_cpy(vram_s, s_fb, SCREEN_W * SCREEN_H * 2);
        }

        /* Process Maple Controller Input */
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if(cont) {
            cont_state_t *st = (cont_state_t *)maple_dev_status(cont);
            if(st) {
                uint32_t pressed = st->buttons & ~prev_buttons;
                prev_buttons = st->buttons;

                if(s_screen == SCREEN_MAIN_MENU) {
                    if(pressed & CONT_DPAD_UP) {
                        s_menu_sel = (s_menu_sel - 1 + MENU_COUNT) % MENU_COUNT;
                        audio_play_click();
                    }
                    if(pressed & CONT_DPAD_DOWN) {
                        s_menu_sel = (s_menu_sel + 1) % MENU_COUNT;
                        audio_play_click();
                    }
                    if(pressed & CONT_START) {
                        disc_launch();
                    }
                    if(pressed & CONT_A) {
                        audio_play_confirm();
                        switch(s_menu_sel) {
                            case 0: disc_launch(); break;
                            case 1: s_screen = SCREEN_SYSINFO; break;
                            case 2: s_screen = SCREEN_MAPLE; break;
                            case 3: s_screen = SCREEN_FLASHROM; break;
                            case 4: s_screen = SCREEN_MEMORY; break;
                            case 5: s_screen = SCREEN_TEST; break;
                            case 6: arch_reboot(); break;
                        }
                    }
                } else if(s_screen == SCREEN_FLASHROM) {
                    flashrom_ui_handle_input(pressed);
                    if(pressed & CONT_B) {
                        audio_play_click();
                        s_screen = SCREEN_MAIN_MENU;
                    }
                } else if(s_screen == SCREEN_MEMORY) {
                    memory_ui_handle_input(pressed);
                    if(pressed & CONT_B) {
                        audio_play_click();
                        s_screen = SCREEN_MAIN_MENU;
                    }
                } else if(s_screen == SCREEN_TEST) {
                    test_ui_handle_input(pressed);
                    if(pressed & CONT_B) {
                        audio_play_click();
                        s_screen = SCREEN_MAIN_MENU;
                    }
                } else {
                    if(pressed & CONT_B) {
                        audio_play_click();
                        s_screen = SCREEN_MAIN_MENU;
                    }
                }
            }
        }

        thd_sleep(16);
    }

    return 0;
}
