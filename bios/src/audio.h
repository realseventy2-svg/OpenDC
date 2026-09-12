#ifndef __BIOS_AUDIO_H
#define __BIOS_AUDIO_H

#include <stdint.h>

void audio_hw_init(void);
void audio_play_click(void);
void audio_play_confirm(void);
void audio_play_tone(int ch, uint32_t pitch, int volume, int pan, int duration_frames);
void audio_update(void);
void audio_stop_all(void);

#endif /* __BIOS_AUDIO_H */
