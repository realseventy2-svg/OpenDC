#include "screen_test.h"
#include "renderer.h"
#include "audio_driver.h"
#include <dc/maple/controller.h>

void screen_test_render(void) {
    uint16_t bars[8] = {
        0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000
    };

    int bar_w = SCREEN_W / 8;
    for(int i = 0; i < 8; i++) {
        draw_rect(i * bar_w, 0, bar_w, 320, bars[i]);
    }

    for(int x = 0; x < SCREEN_W; x++) {
        int level = (x * 31) / SCREEN_W;
        uint16_t gray = (level << 11) | ((level * 2) << 5) | level;
        draw_rect(x, 320, 1, 60, gray);
    }

    draw_text_2x(MARGIN_X, 400, COLOR_WHITE, "VIDEO DAC TEST PATTERN");
    draw_text(MARGIN_X, 436, COLOR_LIGHT_GRAY, "(A) Audio Test Tone   (B) Return to Bootmenu");
}

bios_screen_t screen_test_handle_input(uint32_t pressed) {
    if(pressed & CONT_A) {
        audio_play_tone(0, 0x111C, 12, 0x1F, 20);
        audio_play_tone(1, 0x1A13, 12, 0x00, 20);
    }
    if(pressed & CONT_B) {
        audio_play_click();
        return SCREEN_MAIN_MENU;
    }

    return SCREEN_TEST;
}
