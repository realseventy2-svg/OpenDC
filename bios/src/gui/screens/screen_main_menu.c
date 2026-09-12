#include "screen_main_menu.h"
#include "renderer.h"
#include "disc_service.h"
#include "sysinfo_service.h"
#include "audio_driver.h"
#include <dc/maple/controller.h>
#include <arch/arch.h>
#include <arch/exec.h>

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

static int s_menu_sel = 0;

void screen_main_menu_render(void) {
    int start_x = MARGIN_X;
    int y = 36;
    const disc_info_t *disc = disc_service_get_info();

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
    if(disc->disc_present) {
        draw_text_fmt(start_x, y, COLOR_GRAY,
            ".// Disc: [%s] %s (%s)",
            disc->is_gdrom ? "GD-ROM" : "CD-ROM",
            disc->title,
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
        sysinfo_get_cable_name(), sysinfo_get_region_name());
    y += 18;

    draw_text(start_x, y, COLOR_GRAY,
        ".// Status: System Initialized [ OK ]");

    /* Controls footer */
    draw_text(start_x, 436, COLOR_LIGHT_GRAY,
        "(A) Select   (START) Fast-Boot   (UP/DOWN) Navigate");
}

bios_screen_t screen_main_menu_handle_input(uint32_t pressed) {
    if(pressed & CONT_DPAD_UP) {
        s_menu_sel = (s_menu_sel - 1 + MENU_COUNT) % MENU_COUNT;
        audio_play_click();
    }
    if(pressed & CONT_DPAD_DOWN) {
        s_menu_sel = (s_menu_sel + 1) % MENU_COUNT;
        audio_play_click();
    }
    if(pressed & CONT_START) {
        disc_service_launch();
    }
    if(pressed & CONT_A) {
        audio_play_confirm();
        switch(s_menu_sel) {
            case 0: disc_service_launch(); break;
            case 1: return SCREEN_SYSINFO;
            case 2: return SCREEN_MAPLE;
            case 3: return SCREEN_FLASHROM;
            case 4: return SCREEN_MEMORY;
            case 5: return SCREEN_TEST;
            case 6: arch_reboot(); break;
        }
    }

    return SCREEN_MAIN_MENU;
}
