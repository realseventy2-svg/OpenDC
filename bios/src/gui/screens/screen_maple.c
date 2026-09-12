#include "screen_maple.h"
#include "renderer.h"
#include "maple_service.h"
#include "audio_driver.h"
#include <dc/maple/controller.h>

void screen_maple_render(void) {
    int start_x = MARGIN_X;
    int y = 30;

    draw_text_2x(start_x, y, COLOR_WHITE, "MAPLE BUS CONTROLLER & VMU TOPOLOGY");
    y += 26;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 14;

    char ports[4] = { 'A', 'B', 'C', 'D' };
    for(int p = 0; p < 4; p++) {
        maple_port_info_t info;
        maple_service_poll_port(p, &info);

        draw_text_fmt(start_x, y, COLOR_WHITE, "PORT %c:", ports[p]); y += 14;

        if(info.controller_connected) {
            draw_text_fmt(start_x + 16, y, COLOR_GREEN,
                "Controller: %s (Func: 0x%08X Area: 0x%02X)",
                info.controller_name, (unsigned int)info.functions, info.area_code);
        } else {
            draw_text(start_x + 16, y, COLOR_GRAY, "Controller: [Not Connected]");
        }
        y += 14;

        draw_text_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
            "Slot 1: %-18s | Slot 2: %-18s",
            info.vmu1_connected ? info.vmu1_name : "[Empty]",
            info.vmu2_connected ? info.vmu2_name : "[Empty]");
        y += 20;
    }

    draw_text(start_x, 436, COLOR_LIGHT_GRAY, "(B) Return to Bootmenu");
}

bios_screen_t screen_maple_handle_input(uint32_t pressed) {
    if(pressed & CONT_B) {
        audio_play_click();
        return SCREEN_MAIN_MENU;
    }
    return SCREEN_MAPLE;
}
