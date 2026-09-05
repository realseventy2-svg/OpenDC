#include "sound.h"
#include "math_fx.h"
#include <stdint.h>

/* NOTE: sin_fx, clamp16, udiv32, sdiv32 provided by math_fx.h (static inline) */

/* =========================================================================
 * SERENE SEGA DREAMCAST STARTUP SOUND ENGINE (7.5-SECOND RESOLUTION)
 * =========================================================================
 * Timing Specs (60 FPS VBlank Tick):
 *   - Frame   0 (0.00s): Warm E2 Sub-Bass Foundation + Stereo E3/B3/G#4 Strings
 *   - Frame  36 (0.60s): First Celesta Bell (E4) with Stereo Echo
 *   - Frame  84 (1.40s): Second Celesta Bell (G#4) with Stereo Echo
 *   - Frame 140 (2.33s): Third Singing Bell (B4) with Stereo Echo
 *   - Frame 204 (3.40s): Peaceful Resolution Bell (E5) + Silky Air Bloom + Full Pad
 *   - Frame 330 (5.50s): Smooth 2.0s Natural Perceptual S-Curve Fade-Out Begins
 *   - Frame 450 (7.50s): Total Channel Silence & Master DAC Mute
 *   - Frames 450..479:   Clean 0.5s Silent Buffer before hard 8.0s Game Handoff
 * ========================================================================= */

static const uint16_t MIDI_PITCH_TABLE[88] = {
    /* 21..26 (A0..D1)   */ 0x691C, 0x696A, 0x69BC, 0x6A13, 0x6A70, 0x6AD2,
    /* 27..32 (D#1..G#1) */ 0x6B39, 0x6BA7, 0x700E, 0x704C, 0x708D, 0x70D2,
    /* 33..38 (A1..D2)   */ 0x711C, 0x716A, 0x71BC, 0x7213, 0x7270, 0x72D2,
    /* 39..44 (D#2..G#2) */ 0x7339, 0x73A7, 0x780E, 0x784C, 0x788D, 0x78D2,
    /* 45..50 (A2..D3)   */ 0x791C, 0x796A, 0x79BC, 0x7A13, 0x7A70, 0x7AD2,
    /* 51..56 (D#3..G#3) */ 0x7B39, 0x7BA7, 0x000E, 0x004C, 0x008D, 0x00D2,
    /* 57..62 (A3..D4)   */ 0x011C, 0x016A, 0x01BC, 0x0213, 0x0270, 0x02D2,
    /* 63..68 (D#4..G#4) */ 0x0339, 0x03A7, 0x080E, 0x084C, 0x088D, 0x08D2,
    /* 69..74 (A4..D5)   */ 0x091C, 0x096A, 0x09BC, 0x0A13, 0x0A70, 0x0AD2,
    /* 75..80 (D#5..G#5) */ 0x0B39, 0x0BA7, 0x100E, 0x104C, 0x108D, 0x10D2,
    /* 81..86 (A5..D6)   */ 0x111C, 0x116A, 0x11BC, 0x1213, 0x1270, 0x12D2,
    /* 87..92 (D#6..G#6) */ 0x1339, 0x13A7, 0x180E, 0x184C, 0x188D, 0x18D2,
    /* 93..98 (A6..D7)   */ 0x191C, 0x196A, 0x19BC, 0x1A13, 0x1A70, 0x1AD2,
    /* 99..104 (D#7..G#7)*/ 0x1B39, 0x1BA7, 0x200E, 0x204C, 0x208D, 0x20D2,
    /* 105..108 (A7..C8) */ 0x211C, 0x216A, 0x21BC, 0x2213
};

static int16_t make_velvet_celesta_bell(int i) {
    int32_t s1 = sin_fx(i);
    int32_t s2 = sin_fx(i * 2);
    int32_t s3 = sin_fx(i * 3);
    int32_t val = (s1 * 98) + (s2 * 14) + (s3 * 4);
    return (int16_t)clamp16(val);
}

static int16_t make_serene_ambient_pad(int i) {
    int32_t p1 = sin_fx(i);
    int32_t p2 = sin_fx(i * 2);
    int32_t p3 = sin_fx(i * 3);
    int32_t val = (p1 * 92) + (p2 * 18) + (p3 * 6);
    return (int16_t)clamp16(val);
}

static int16_t make_warm_acoustic_bass(int i) {
    int32_t b1 = sin_fx(i);
    int32_t b2 = sin_fx(i * 2);
    int32_t val = (b1 * 100) + (b2 * 20);
    return (int16_t)clamp16(val);
}

static int16_t make_silky_air_bloom(int i) {
    int32_t s1 = sin_fx(i * 2);
    int32_t s2 = sin_fx(i * 4);
    int32_t val = (s1 * 60) + (s2 * 25);
    return (int16_t)clamp16(val);
}

static uint32_t s_seq_frame = 0;
static int s_sound_initialized = 0;
static int s_total_duration = 280;

void sound_init(void) {
    /* 1. Hold AICA ARM7 sound CPU in reset */
    *(volatile uint32_t *)0xA0702C00UL |= 1;

    /* 2. Configure master dry volume output */
    *(volatile uint16_t *)0xA0702800UL = 0x000E;

    /* 3. Stop and mute all 64 AICA hardware channels */
    for (int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0F00; /* DISDL = 15 (silence) */
    }

    /* 4. Synthesize single-cycle wavetables at exact byte offsets */
    volatile int16_t *wav_bell    = (volatile int16_t *)((uintptr_t)AICA_RAM_BASE + 0);
    volatile int16_t *wav_pad     = (volatile int16_t *)((uintptr_t)AICA_RAM_BASE + 512);
    volatile int16_t *wav_bass    = (volatile int16_t *)((uintptr_t)AICA_RAM_BASE + 1024);
    volatile int16_t *wav_shimmer = (volatile int16_t *)((uintptr_t)AICA_RAM_BASE + 1536);

    for (int i = 0; i < 256; i++) {
        wav_bell[i]    = make_velvet_celesta_bell(i);
        wav_pad[i]     = make_serene_ambient_pad(i);
        wav_bass[i]    = make_warm_acoustic_bass(i);
        wav_shimmer[i] = make_silky_air_bloom(i);
    }

    s_seq_frame = 0;
    s_total_duration = 280;
    s_sound_initialized = 1;
}

void sound_set_duration(int total_frames) {
    if (total_frames <= 0) total_frames = 480;
    s_total_duration = total_frames;
    s_seq_frame = 0;
}

void sound_play_note(int ch, int midi_note, int volume, int pan, int wavetable_id) {
    if (ch < 0 || ch >= 64 || !s_sound_initialized) return;

    if (midi_note < 21)  midi_note = 21;
    if (midi_note > 108) midi_note = 108;

    uint32_t pitch = MIDI_PITCH_TABLE[midi_note - 21];
    uint32_t smp_offset;

    switch (wavetable_id) {
        case 1:  smp_offset = 512;  break;
        case 2:  smp_offset = 1024; break;
        case 3:  smp_offset = 1536; break;
        default: smp_offset = 0;    break;
    }

    /* Key off and set sample addresses */
    AICA_CHN_REG(ch, 0x00) = 0x8000;
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;

    /* Hardware ADSR Envelopes */
    if (wavetable_id == 0) {
        /* Celesta Bell: quick rise (AR=22), natural acoustic release (RR=14) */
        AICA_CHN_REG(ch, 0x10) = (1 << 11) | (5 << 6) | 22;
        AICA_CHN_REG(ch, 0x14) = (0 << 10) | (0 << 5) | 14;
    } else if (wavetable_id == 1) {
        /* Ambient Strings: soft rise (AR=18), smooth sustained body (RR=12) */
        AICA_CHN_REG(ch, 0x10) = (1 << 11) | (2 << 6) | 18;
        AICA_CHN_REG(ch, 0x14) = (0 << 10) | (0 << 5) | 12;
    } else if (wavetable_id == 2) {
        /* Acoustic Bass: rounded rise (AR=20), warm body (RR=13) */
        AICA_CHN_REG(ch, 0x10) = (1 << 11) | (3 << 6) | 20;
        AICA_CHN_REG(ch, 0x14) = (0 << 10) | (0 << 5) | 13;
    } else {
        /* Silky Shimmer: slow swell (AR=16), delicate tail (RR=10) */
        AICA_CHN_REG(ch, 0x10) = (1 << 11) | (1 << 6) | 16;
        AICA_CHN_REG(ch, 0x14) = (0 << 10) | (0 << 5) | 10;
    }

    AICA_CHN_REG(ch, 0x18) = pitch;

    /* Direct Send Level attenuation: 15 = 0 dB (full volume), 0 = -90 dB */
    uint32_t disdl = (volume >= 17) ? 0 : (17 - (volume & 0x0F));
    AICA_CHN_REG(ch, 0x24) = (disdl << 8) | (pan & 0x1F);

    /* Bypass LPF and trigger key-on */
    AICA_CHN_REG(ch, 0x28) = 0x0024;
    AICA_CHN_REG(ch, 0x00) = 0xC200 | (smp_offset >> 16);
}

void sound_stop_channel(int ch) {
    if (ch < 0 || ch >= 64) return;
    AICA_CHN_REG(ch, 0x00) = 0x8000;
}

void sound_stop(void) {
    if (!s_sound_initialized) return;
    for (int ch = 0; ch < 32; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0F00;
    }
    *(volatile uint16_t *)0xA0702800UL = 0x0000;
    s_sound_initialized = 0;
}

void sound_tick(void) {
    if (!s_sound_initialized) return;

    uint32_t tick = s_seq_frame;

    /* Dynamic melody triggers based on configured total duration */
    uint32_t t_intro, t_chime1, t_chime2, t_chime3, t_climax;
    if (s_total_duration <= 270) {
        /* 4.0s startup pacing */
        t_intro  = 2;
        t_chime1 = 24;   /* 0.40s */
        t_chime2 = 60;   /* 1.00s */
        t_chime3 = 96;   /* 1.60s */
        t_climax = 132;  /* 2.20s */
    } else {
        /* Extended 8.0s+ soothing startup pacing */
        t_intro  = 2;
        t_chime1 = 36;   /* 0.60s */
        t_chime2 = 84;   /* 1.40s */
        t_chime3 = 138;  /* 2.30s */
        t_climax = 198;  /* 3.30s */
    }

    if (tick == t_intro) {
        /* Warm Sub-Bass Foundation (E2 = MIDI 40) - Mellow level 9 */
        sound_play_note(0, 40, 9, 0x00, 2);

        /* Serene Ambient Strings in Stereo - Soft level 8 */
        sound_play_note(1, 52, 8, 0x1E, 1); /* E3  (Left) */
        sound_play_note(2, 59, 8, 0x0E, 1); /* B3  (Right) */
        sound_play_note(3, 68, 8, 0x1A, 1); /* G#4 (Left) */
        sound_play_note(4, 75, 7, 0x0A, 1); /* D#5 (Right) */
    } else if (tick == t_chime1) {
        /* First peaceful felt bell (E4 = MIDI 64) */
        sound_play_note(5, 64, 11, 0x18, 0); /* Left-Center Warm Chime (level 11) */
        sound_play_note(6, 64, 8,  0x08, 0); /* Subtle Stereo Ambient Echo (level 8) */
    } else if (tick == t_chime2) {
        /* Second contemplative bell (G#4 = MIDI 68) */
        sound_play_note(7, 68, 11, 0x08, 0); /* Right-Center Warm Chime (level 11) */
        sound_play_note(8, 68, 8,  0x18, 0); /* Subtle Stereo Ambient Echo (level 8) */
    } else if (tick == t_chime3) {
        /* Third singing bell (B4 = MIDI 71) */
        sound_play_note(5, 71, 11, 0x16, 0); /* Left-Center Warm Chime (level 11) */
        sound_play_note(6, 71, 8,  0x0A, 0); /* Subtle Stereo Ambient Echo (level 8) */
    } else if (tick == t_climax) {
        /* Peaceful Resolution Bell (E5) */
        sound_play_note(9,  76, 11, 0x00, 0); /* Center Melody */
        sound_play_note(10, 76, 9,  0x1E, 0); /* Soft Left Chorus */
        sound_play_note(11, 76, 9,  0x0E, 0); /* Soft Right Chorus */

        /* Silky Stardust Air Bloom in Stereo */
        sound_play_note(12, 68, 7, 0x1E, 3); /* G#4 air shimmer */
        sound_play_note(13, 68, 7, 0x0E, 3); /* G#4 air shimmer */

        /* Ambient Strings Full Warmth Bloom */
        sound_play_note(1, 52, 9, 0x1E, 1); /* E3 */
        sound_play_note(2, 64, 9, 0x00, 1); /* E4 */
        sound_play_note(3, 71, 9, 0x0E, 1); /* B4 */
        sound_play_note(4, 76, 8, 0x1A, 1); /* E5 */
    }

    /* 1.5-Second Master Fade-Out across final 90 frames of duration */
    int fade_len = 90;
    if (s_total_duration <= 120) {
        fade_len = s_total_duration >> 1;
    }
    int fade_start = s_total_duration - fade_len;
    if (fade_start < 0) fade_start = 0;

    if (tick >= (uint32_t)fade_start && tick <= (uint32_t)s_total_duration) {
        static const uint8_t s_fade_curve[91] = {
            14, 14, 14, 14, 14, 14, 14, 13, 13, 13,
            13, 13, 13, 13, 12, 12, 12, 12, 12, 12,
            12, 12, 11, 11, 11, 11, 11, 11, 11, 10,
            10, 10, 10, 10, 10,  9,  9,  9,  9,  9,
             8,  8,  8,  8,  8,  8,  7,  7,  7,  7,
             7,  6,  6,  6,  6,  6,  5,  5,  5,  5,
             5,  4,  4,  4,  4,  4,  3,  3,  3,  3,
             3,  2,  2,  2,  2,  2,  2,  1,  1,  1,
             1,  1,  1,  1,  1,  0,  0,  0,  0,  0, 0
        };
        int offset = (int)tick - fade_start;
        int idx = (fade_len > 0) ? sdiv32(offset * 90, fade_len) : 90;
        if (idx < 0) idx = 0;
        if (idx > 90) idx = 90;
        *(volatile uint16_t *)0xA0702800UL = s_fade_curve[idx];
    } else if (tick > (uint32_t)s_total_duration) {
        *(volatile uint16_t *)0xA0702800UL = 0x0000;
    }

    s_seq_frame++;
}