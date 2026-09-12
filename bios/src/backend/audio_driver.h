#ifndef __BIOS_AUDIO_DRIVER_H
#define __BIOS_AUDIO_DRIVER_H

#include <stdint.h>

void audio_driver_init(void);
void audio_play_click(void);
void audio_play_confirm(void);
void audio_play_tone(int ch, uint32_t pitch, int volume, int pan, int duration_frames);
void audio_driver_update(void);
void audio_driver_stop_all(void);

#endif /* __BIOS_AUDIO_DRIVER_H */
