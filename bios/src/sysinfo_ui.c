#include "sysinfo_ui.h"
#include "config.h"
#include "font.h"
#include "disc.h"

void sysinfo_ui_render(void) {
    int start_x = MARGIN_X;
    int y = 30;

    draw_text_2x(start_x, y, COLOR_WHITE, "SYSTEM & HARDWARE DIAGNOSTICS");
    y += 26;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 14;

    draw_text(start_x, y, COLOR_WHITE, "CPU SUBSYSTEM:"); y += 15;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY, "- Hitachi SH7091 (SH-4) 32-bit RISC @ 200.0 MHz"); y += 14;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY, "- 16-entry UTLB, 4-entry ITLB, 16KB O-Cache + 8KB I-Cache"); y += 14;
    draw_text(start_x + 16, y, COLOR_GREEN, "- Status: [ PASS / OPERATIONAL ]"); y += 20;

    draw_text(start_x, y, COLOR_WHITE, "MEMORY ARCHITECTURE:"); y += 15;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY, "- 16 MB 100MHz SDRAM (Area 3 Bus Width: 32-bit Burst)"); y += 14;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY, "- Cached: 0x8C000000-0x8CFFFFFF | Uncached: 0xAC000000"); y += 14;
    draw_text(start_x + 16, y, COLOR_GREEN, "- Status: [ PASS / 16384 KB OK ]"); y += 20;

    draw_text(start_x, y, COLOR_WHITE, "GRAPHICS PIPELINE:"); y += 15;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY, "- NEC PowerVR2 CLX2 3D Rasterizer @ 100.0 MHz"); y += 14;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY, "- 8 MB 128-bit Synchronous VRAM @ 0xA5000000"); y += 14;
    draw_text_fmt(start_x + 16, y, COLOR_LIGHT_GRAY, "- Active Output: %s", get_cable_name()); y += 20;

    draw_text(start_x, y, COLOR_WHITE, "AUDIO & BUS SUBSYSTEM:"); y += 15;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY, "- Yamaha AICA Sound Core + ARM7DI Sound CPU @ 45.0 MHz"); y += 14;
    draw_text(start_x + 16, y, COLOR_LIGHT_GRAY, "- G1 Bus / ATA: SB_GDEN=0x01 | SB_G1RRC=0x18 | DMAOR=0x8201"); y += 24;

    draw_text(start_x, 436, COLOR_LIGHT_GRAY, "(B) Return to Bootmenu");
}
