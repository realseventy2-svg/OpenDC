#include "screen_memory.h"
#include "renderer.h"
#include "memory_service.h"
#include "audio_driver.h"
#include <dc/maple/controller.h>

static uint32_t s_mem_addr = 0x8C000000UL;

void screen_memory_render(void) {
    int start_x = MARGIN_X;
    int y = 24;

    draw_text_2x(start_x, y, COLOR_WHITE, "MEMORY & REGISTER HEX INSPECTOR");
    y += 24;
    draw_text_fmt(start_x, y, COLOR_GOLD,
        "BASE ADDRESS: 0x%08X  |  [X] SDRAM (0x8C000000)  [Y] BIOS ROM (0xA0000000)",
        (unsigned int)s_mem_addr);
    y += 15;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 8;

    draw_text(start_x, y, COLOR_WHITE,
        "ADDRESS    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F   ASCII");
    y += 14;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 6;

    for(int row = 0; row < 16; row++) {
        uint32_t addr = s_mem_addr + (row * 16);
        char hex1[32] = {0};
        char hex2[32] = {0};
        char ascii[17] = {0};

        memory_service_format_hex_row(addr, hex1, hex2, ascii);

        draw_text_fmt(start_x, y, COLOR_LIGHT_GRAY,
            "%08X   %s %s  %s", (unsigned int)addr, hex1, hex2, ascii);
        y += 14;
    }

    draw_text(start_x, 436, COLOR_LIGHT_GRAY,
        "(UP/DN) +/-16B   (L/R) +/-256B   (X) RAM   (Y) ROM   (B) Exit");
}

bios_screen_t screen_memory_handle_input(uint32_t pressed) {
    if(pressed & CONT_DPAD_UP)    s_mem_addr -= 16;
    if(pressed & CONT_DPAD_DOWN)  s_mem_addr += 16;
    if(pressed & CONT_DPAD_LEFT)  s_mem_addr -= 256;
    if(pressed & CONT_DPAD_RIGHT) s_mem_addr += 256;
    if(pressed & CONT_X)          s_mem_addr = 0x8C000000UL;
    if(pressed & CONT_Y)          s_mem_addr = 0xA0000000UL;
    if(pressed & CONT_B) {
        audio_play_click();
        return SCREEN_MAIN_MENU;
    }

    return SCREEN_MEMORY;
}
