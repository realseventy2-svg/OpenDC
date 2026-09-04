#include "sound.h"

/* Fixed 60-note pitch table for MIDI notes 36 (C2) to 95 (B6).
   AICA pitch register format: (OCT << 11) | FNS */
static const uint16_t MIDI_PITCH_TABLE[60] = {
    /* 36..41 (C2..F2)   */ 0x7213, 0x7270, 0x72D2, 0x7339, 0x73A7, 0x780E,
    /* 42..47 (F#2..B2)  */ 0x784C, 0x788D, 0x78D2, 0x791C, 0x796A, 0x79BC,
    /* 48..53 (C3..F3)   */ 0x7A13, 0x7A70, 0x7AD2, 0x7B39, 0x7BA7, 0x000E,
    /* 54..59 (F#3..B3)  */ 0x004C, 0x008D, 0x00D2, 0x011C, 0x016A, 0x01BC,
    /* 60..65 (C4..F4)   */ 0x0213, 0x0270, 0x02D2, 0x0339, 0x03A7, 0x080E,
    /* 66..71 (F#4..B4)  */ 0x084C, 0x088D, 0x08D2, 0x091C, 0x096A, 0x09BC,
    /* 72..77 (C5..F5)   */ 0x0A13, 0x0A70, 0x0AD2, 0x0B39, 0x0BA7, 0x100E,
    /* 78..83 (F#5..B5)  */ 0x104C, 0x108D, 0x10D2, 0x111C, 0x116A, 0x11BC,
    /* 84..89 (C6..F6)   */ 0x1213, 0x1270, 0x12D2, 0x1339, 0x13A7, 0x180E,
    /* 90..95 (F#6..B6)  */ 0x184C, 0x188D, 0x18D2, 0x191C, 0x196A, 0x19BC
};

/* 8.8 Fixed-Point Sine Quarter-Wave Table (0 to 90 degrees in 64 steps, 256 = 1.0) */
static const int16_t sin_quarter_table[65] = {
    0,   6,  12,  18,  25,  31,  37,  43,  49,  56,  62,  68,  74,  80,  86,  92,
   97, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176,
  181, 185, 189, 193, 197, 201, 205, 208, 212, 215, 219, 222, 225, 228, 231, 233,
  236, 238, 240, 242, 244, 246, 247, 249, 250, 251, 252, 253, 254, 254, 255, 255,
  256
};

static int32_t sin_fx(int angle) {
    angle &= 0xFF;
    if (angle <= 64) return sin_quarter_table[angle];
    if (angle <= 128) return sin_quarter_table[128 - angle];
    if (angle <= 192) return -sin_quarter_table[angle - 128];
    return -sin_quarter_table[256 - angle];
}

static uint32_t s_seq_frame = 0;
static int s_sound_initialized = 0;

void sound_init(void) {
    /* 1. Hold ARM sound CPU in reset while SH-4 configures SPU */
    *(volatile uint32_t *)0xA0702C00UL |= 1;

    /* 2. Unmute master dry output volume */
    *(volatile uint16_t *)0xA0702800UL = 0x000F;

    /* 3. Stop and silence all 64 AICA hardware channels */
    for (int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000; /* Key off */
        AICA_CHN_REG(ch, 0x24) = 0x0000; /* Zero volume */
    }

    /* 4. Synthesize 256-sample 16-bit single-cycle PCM wavetables in SPU RAM */
    volatile int16_t *wav_pad   = (volatile int16_t *)(AICA_RAM_BASE + 0);
    volatile int16_t *wav_chime = (volatile int16_t *)(AICA_RAM_BASE + 512);

    for (int i = 0; i < 256; i++) {
        /* Warm Ambient Pad: rich harmonic blend */
        int32_t s1 = sin_fx(i);
        int32_t s2 = sin_fx(i * 2);
        int32_t s3 = sin_fx(i * 3);
        int32_t val_pad = (s1 * 90) + (s2 * 30) + (s3 * 15);
        if (val_pad > 32767) val_pad = 32767;
        if (val_pad < -32767) val_pad = -32767;
        wav_pad[i] = (int16_t)val_pad;

        /* Crystal Bell / Chime: sparkling fundamental + high shimmer */
        int32_t c1 = sin_fx(i);
        int32_t c3 = sin_fx(i * 3);
        int32_t c5 = sin_fx(i * 5);
        int32_t val_chime = (c1 * 95) + (c3 * 25) + (c5 * 10);
        if (val_chime > 32767) val_chime = 32767;
        if (val_chime < -32767) val_chime = -32767;
        wav_chime[i] = (int16_t)val_chime;
    }

    s_seq_frame = 0;
    s_sound_initialized = 1;
}

void sound_play_note(int ch, int midi_note, int volume, int pan, int wavetable_id) {
    if (ch < 0 || ch >= 64 || !s_sound_initialized) return;

    /* Clamp MIDI note range (36 to 95) */
    if (midi_note < 36) midi_note = 36;
    if (midi_note > 95) midi_note = 95;

    uint32_t pitch = MIDI_PITCH_TABLE[midi_note - 36];
    uint32_t smp_offset = (wavetable_id == 1) ? 512 : 0;

    /* Stop previous note on channel */
    AICA_CHN_REG(ch, 0x00) = 0x8000;

    /* Sample memory start address */
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;

    /* 256-sample looping boundaries */
    AICA_CHN_REG(ch, 0x08) = 0;   /* Loop start */
    AICA_CHN_REG(ch, 0x0C) = 256; /* Loop end */

    /* ADSR Amplitude Envelope */
    if (wavetable_id == 0) {
        /* Warm Ambient Pad (Smooth Attack, Lush Release) */
        AICA_CHN_REG(ch, 0x10) = 0x0210;
        AICA_CHN_REG(ch, 0x14) = 0x3C8C;
    } else {
        /* Crystal Chime Bell (Instant Attack, Resonant Decay) */
        AICA_CHN_REG(ch, 0x10) = 0x751F;
        AICA_CHN_REG(ch, 0x14) = 0x3D14;
    }

    /* Pitch register */
    AICA_CHN_REG(ch, 0x18) = pitch;

    /* Direct Send Volume (bits 11-8) and Panning (bits 4-0) */
    AICA_CHN_REG(ch, 0x24) = ((uint32_t)(volume & 0x0F) << 8) | (pan & 0x1F);

    /* Bypass Low Pass Filter */
    AICA_CHN_REG(ch, 0x28) = 0x0024;

    /* Execute Key-On: 16-bit PCM (PCMS=0), Looped (LPCTL=1), SA high */
    AICA_CHN_REG(ch, 0x00) = 0xC200 | (smp_offset >> 16);
}

void sound_stop_channel(int ch) {
    if (ch < 0 || ch >= 64) return;
    AICA_CHN_REG(ch, 0x00) = 0x8000;
}

void sound_stop(void) {
    if (!s_sound_initialized) return;

    for (int ch = 0; ch < 16; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }
    *(volatile uint16_t *)0xA0702800UL = 0x0000;
    s_sound_initialized = 0;
}

void sound_tick(void) {
    if (!s_sound_initialized) return;

    /* 4-bar atmospheric ambient chord sequence (240 frames total @ 60 FPS) */
    uint32_t tick = s_seq_frame % 240;

    switch (tick) {
        /* --- Bar 1: D Major 9th (Dreamy & Ethereal) --- */
        case 0:
            sound_play_note(0, 50, 13, 0x16, 0); /* D3  Pad L */
            sound_play_note(1, 57, 12, 0x0A, 0); /* A3  Pad R */
            sound_play_note(2, 61, 11, 0x1A, 0); /* C#4 Pad L */
            sound_play_note(3, 66, 11, 0x06, 0); /* F#4 Pad R */
            sound_play_note(4, 73, 14, 0x00, 1); /* C#5 Chime Center */
            break;
        case 15:
            sound_play_note(5, 76, 13, 0x0B, 1); /* E5  Chime R */
            break;
        case 30:
            sound_play_note(4, 78, 14, 0x1B, 1); /* F#5 Chime L */
            break;
        case 45:
            sound_play_note(5, 81, 13, 0x0E, 1); /* A5  Chime R */
            break;

        /* --- Bar 2: B Minor 9th (Atmospheric) --- */
        case 60:
            sound_play_note(0, 47, 13, 0x16, 0); /* B2  Pad L */
            sound_play_note(1, 54, 12, 0x0A, 0); /* F#3 Pad R */
            sound_play_note(2, 59, 11, 0x1A, 0); /* B3  Pad L */
            sound_play_note(3, 62, 11, 0x06, 0); /* D4  Pad R */
            sound_play_note(4, 73, 14, 0x18, 1); /* C#5 Chime L */
            break;
        case 75:
            sound_play_note(5, 74, 13, 0x08, 1); /* D5  Chime R */
            break;
        case 90:
            sound_play_note(4, 78, 14, 0x1B, 1); /* F#5 Chime L */
            break;
        case 105:
            sound_play_note(5, 81, 13, 0x0E, 1); /* A5  Chime R */
            break;

        /* --- Bar 3: G Major 7th #11 (Cosmic Space) --- */
        case 120:
            sound_play_note(0, 43, 14, 0x16, 0); /* G2  Pad L */
            sound_play_note(1, 50, 12, 0x0A, 0); /* D3  Pad R */
            sound_play_note(2, 54, 12, 0x1A, 0); /* F#3 Pad L */
            sound_play_note(3, 59, 11, 0x06, 0); /* B3  Pad R */
            sound_play_note(4, 78, 14, 0x00, 1); /* F#5 Chime Center */
            break;
        case 135:
            sound_play_note(5, 82, 13, 0x0A, 1); /* A#5 Chime R */
            break;
        case 150:
            sound_play_note(4, 83, 14, 0x18, 1); /* B5  Chime L */
            break;
        case 165:
            sound_play_note(5, 85, 14, 0x0E, 1); /* C#6 Chime R */
            break;

        /* --- Bar 4: A 7th sus4 -> A Major (Uplifting Sega Resolution) --- */
        case 180:
            sound_play_note(0, 45, 14, 0x16, 0); /* A2  Pad L */
            sound_play_note(1, 52, 12, 0x0A, 0); /* E3  Pad R */
            sound_play_note(2, 57, 12, 0x1A, 0); /* A3  Pad L */
            sound_play_note(3, 62, 11, 0x06, 0); /* D4  Pad R (sus4) */
            sound_play_note(4, 81, 14, 0x18, 1); /* A5  Chime L */
            break;
        case 195:
            sound_play_note(5, 85, 14, 0x08, 1); /* C#6 Chime R */
            break;
        case 210:
            sound_play_note(3, 61, 12, 0x06, 0); /* C#4 Pad R (Resolves to Major) */
            sound_play_note(4, 88, 15, 0x00, 1); /* E6  Chime Center (Crescendo) */
            break;
        case 225:
            sound_play_note(5, 81, 13, 0x0B, 1); /* A5  Chime R */
            break;

        default:
            break;
    }

    s_seq_frame++;
}
