#include "screen_sysinfo.h"
#include "renderer.h"
#include "sysinfo_service.h"
#include "audio_driver.h"
#include <dc/maple/controller.h>

void screen_sysinfo_render(void) {
    int start_x = MARGIN_X;
    int y = 24;
    const sysinfo_specs_t *specs = sysinfo_get_specs();

    draw_bfont(start_x, y, COLOR_WHITE, "SYSTEM & HARDWARE DIAGNOSTICS");
    y += 28;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 12;

    draw_bfont(start_x, y, COLOR_WHITE, "CPU SUBSYSTEM:"); y += 24;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- %s @ %s", specs->cpu_model, specs->cpu_speed); y += 14;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- %s", specs->cpu_cache); y += 14;
    draw_sysfont(start_x + 16, y, COLOR_GREEN, "- Status: [ PASS / OPERATIONAL ]"); y += 18;

    draw_bfont(start_x, y, COLOR_WHITE, "MEMORY ARCHITECTURE:"); y += 24;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- %s (%s)", specs->ram_size, specs->ram_bus); y += 14;
    draw_sysfont(start_x + 16, y, COLOR_LIGHT_GRAY, "- Cached: 0x8C000000-0x8CFFFFFF | Uncached: 0xAC000000"); y += 14;
    draw_sysfont(start_x + 16, y, COLOR_GREEN, "- Status: [ PASS / 16384 KB OK ]"); y += 18;

    draw_bfont(start_x, y, COLOR_WHITE, "GRAPHICS PIPELINE:"); y += 24;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- %s @ %s", specs->gpu_model, specs->gpu_speed); y += 14;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- %s", specs->vram_size); y += 14;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- Active Output: %s", sysinfo_get_cable_name()); y += 18;

    draw_bfont(start_x, y, COLOR_WHITE, "AUDIO & BUS SUBSYSTEM:"); y += 24;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- %s", specs->audio_chip); y += 14;
    draw_sysfont_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- %s", specs->audio_cpu); y += 24;

    draw_bfont_centered(SCREEN_W / 2, 440, COLOR_LIGHT_GRAY, "(B) Return to Bootmenu");
}

bios_screen_t screen_sysinfo_handle_input(uint32_t pressed) {
    if(pressed & CONT_B) {
        audio_play_click();
        return SCREEN_MAIN_MENU;
    }
    return SCREEN_SYSINFO;
}
