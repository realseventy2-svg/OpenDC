#ifndef OPENDC_BOOTLOADER_SOUND_H
#define OPENDC_BOOTLOADER_SOUND_H

#include <stdint.h>
#include <stddef.h>

/* AICA SPU Base Addresses */
#define AICA_REG_BASE       0xA0700000UL
#define AICA_RAM_BASE       0xA0800000UL

/* AICA Direct Channel Register (0 to 63) */
#define AICA_CHN_REG(ch, reg) (*(volatile uint32_t *)(AICA_REG_BASE + ((ch) * 0x80) + (reg)))

/* Initialize AICA hardware, master volume, and generate ambient wavetables */
void sound_init(void);

/* Set the total boot duration frames for sequencer scaling and fade-out */
void sound_set_duration(int total_frames);

/* Advance the ambient MIDI sequencer by 1 frame (called at 60 FPS on VBlank) */
void sound_tick(void);

/* Play a MIDI note on an AICA channel */
void sound_play_note(int ch, int midi_note, int volume, int pan, int wavetable_id);

/* Play a raw PCM sample / cue on an AICA channel */
void sound_play_cue(int ch, uint32_t spu_addr, uint32_t sample_count, uint16_t pitch_freq, uint8_t vol, uint8_t pan, uint8_t loop);

/* Stop / Key-off a specific AICA channel */
void sound_stop_channel(int ch);

/* Stop all channels and silence AICA before game launch */
void sound_stop(void);

#endif /* OPENDC_BOOTLOADER_SOUND_H */
