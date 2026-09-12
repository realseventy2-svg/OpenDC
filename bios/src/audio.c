#include "audio.h"

#define AICA_REG_BASE 0xA0700000UL
#define AICA_RAM_BASE 0xA0800000UL
#define AICA_CHN_REG(ch, reg) (*(volatile uint32_t *)(AICA_REG_BASE + ((ch) * 0x80) + (reg)))

static int s_audio_timer = 0;

void audio_hw_init(void) {
    /* Mute all AICA channels on startup */
    *(volatile uint32_t *)0xA0702C00UL |= 1;
    *(volatile uint16_t *)0xA0702800UL = 0x000F;
    for(int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }
}

void audio_play_tone(int ch, uint32_t pitch, int volume, int pan, int duration_frames) {
    (void)ch; (void)pitch; (void)volume; (void)pan;
    s_audio_timer = duration_frames;
}

void audio_stop_all(void) {
    for(int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
    }
}

void audio_play_click(void) {
    /* Silent on navigation for authentic devkit terminal experience */
}

void audio_play_confirm(void) {
    /* Silent on selection */
}

void audio_update(void) {
    if(s_audio_timer > 0) {
        s_audio_timer--;
        if(s_audio_timer == 0) {
            audio_stop_all();
        }
    }
}
