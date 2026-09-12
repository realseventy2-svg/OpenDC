#include "maple_ui.h"
#include "config.h"
#include "font.h"
#include <kos.h>
#include <dc/maple.h>

void maple_ui_render(void) {
    int start_x = MARGIN_X;
    int y = 30;

    draw_text_2x(start_x, y, COLOR_WHITE, "MAPLE BUS CONTROLLER & VMU TOPOLOGY");
    y += 26;
    draw_rect(start_x, y, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);
    y += 14;

    char ports[4] = { 'A', 'B', 'C', 'D' };
    for(int p = 0; p < 4; p++) {
        maple_device_t *dev = maple_enum_dev(p, 0);
        maple_device_t *vmu1 = maple_enum_dev(p, 1);
        maple_device_t *vmu2 = maple_enum_dev(p, 2);

        draw_text_fmt(start_x, y, COLOR_WHITE, "PORT %c:", ports[p]); y += 14;

        if(dev) {
            char dev_name[24] = {0};
            get_clean_str(dev_name, dev->info.product_name, 20);
            draw_text_fmt(start_x + 16, y, COLOR_GREEN,
                "Controller: %s (Func: 0x%08X Area: 0x%02X)",
                dev_name, (unsigned int)dev->info.functions, dev->info.area_code);
        } else {
            draw_text(start_x + 16, y, COLOR_GRAY, "Controller: [Not Connected]");
        }
        y += 14;

        char vmu1_name[24] = {0};
        char vmu2_name[24] = {0};
        if(vmu1) get_clean_str(vmu1_name, vmu1->info.product_name, 16);
        if(vmu2) get_clean_str(vmu2_name, vmu2->info.product_name, 16);

        draw_text_fmt(start_x + 16, y, COLOR_LIGHT_GRAY,
            "Slot 1: %-18s | Slot 2: %-18s",
            vmu1 ? vmu1_name : "[Empty]",
            vmu2 ? vmu2_name : "[Empty]");
        y += 20;
    }

    draw_text(start_x, 436, COLOR_LIGHT_GRAY, "(B) Return to Bootmenu");
}
