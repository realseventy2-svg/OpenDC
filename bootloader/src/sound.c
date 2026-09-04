#include "sound.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * MIDI -> AICA Pitch Register Table
 *
 * MIDI notes 24 (C1) through 107 (B7)
 * AICA pitch register format: (OCT << 11) | FNS
 * ------------------------------------------------------------------------- */
static const uint16_t MIDI_PITCH_TABLE[84] = {
    /* 24..29 (C1..F1)   */ 0x6A13, 0x6A70, 0x6AD2, 0x6B39, 0x6BA7, 0x700E,
    /* 30..35 (F#1..B1)  */ 0x704C, 0x708D, 0x70D2, 0x711C, 0x716A, 0x71BC,
    /* 36..41 (C2..F2)   */ 0x7213, 0x7270, 0x72D2, 0x7339, 0x73A7, 0x780E,
    /* 42..47 (F#2..B2)  */ 0x784C, 0x788D, 0x78D2, 0x791C, 0x796A, 0x79BC,
    /* 48..53 (C3..F3)   */ 0x7A13, 0x7A70, 0x7AD2, 0x7B39, 0x7BA7, 0x000E,
    /* 54..59 (F#3..B3)  */ 0x004C, 0x008D, 0x00D2, 0x011C, 0x016A, 0x01BC,
    /* 60..65 (C4..F4)   */ 0x0213, 0x0270, 0x02D2, 0x0339, 0x03A7, 0x080E,
    /* 66..71 (F#4..B4)  */ 0x084C, 0x088D, 0x08D2, 0x091C, 0x096A, 0x09BC,
    /* 72..77 (C5..F5)   */ 0x0A13, 0x0A70, 0x0AD2, 0x0B39, 0x0BA7, 0x100E,
    /* 78..83 (F#5..B5)  */ 0x104C, 0x108D, 0x10D2, 0x111C, 0x116A, 0x11BC,
    /* 84..89 (C6..F6)   */ 0x1213, 0x1270, 0x12D2, 0x1339, 0x13A7, 0x180E,
    /* 90..95 (F#6..B6)  */ 0x184C, 0x188D, 0x18D2, 0x191C, 0x196A, 0x19BC,
    /* 96..101 (C7..F7)  */ 0x1A13, 0x1A70, 0x1AD2, 0x1B39, 0x1BA7, 0x200E,
    /* 102..107 (F#7..B7)*/ 0x204C, 0x208D, 0x20D2, 0x211C, 0x216A, 0x21BC
};

/* -------------------------------------------------------------------------
 * 8.8 Fixed-Point Sine Quarter-Wave Table (0 to 90 degrees in 64 steps)
 * ------------------------------------------------------------------------- */
static const int16_t sin_quarter_table[65] = {
      0,   6,  12,  18,  25,  31,  37,  43,
     49,  56,  62,  68,  74,  80,  86,  92,
     97, 103, 109, 115, 120, 126, 131, 136,
    142, 147, 152, 157, 162, 167, 171, 176,
    181, 185, 189, 193, 197, 201, 205, 208,
    212, 215, 219, 222, 225, 228, 231, 233,
    236, 238, 240, 242, 244, 246, 247, 249,
    250, 251, 252, 253, 254, 254, 255, 255,
    256
};

static int32_t sin_fx(int angle) {
    angle &= 0xFF;
    if (angle <= 64)  return sin_quarter_table[angle];
    if (angle <= 128) return sin_quarter_table[128 - angle];
    if (angle <= 192) return -sin_quarter_table[angle - 128];
    return -sin_quarter_table[256 - angle];
}

static int32_t clamp16(int32_t value) {
    if (value > 32767)  return 32767;
    if (value < -32768) return -32768;
    return value;
}

/*
 * Authentic Dreamcast Chime Wavetable:
 * Bright, crystalline tine with sharp bell overtone structure (1st, 3rd, 5th, and 8th).
 */
static int16_t make_dc_chime(int i) {
    int32_t f1 = sin_fx(i);
    int32_t f3 = sin_fx(i * 3);
    int32_t f5 = sin_fx(i * 5);
    int32_t f8 = sin_fx(i * 8);

    int32_t val = (f1 * 84) + (f3 * 34) + (f5 * 18) + (f8 * 6);
    return (int16_t)clamp16(val);
}

/*
 * Deep Sub Thump / Sub-Bass Wavetable:
 * Extremely clean low-end fundamental with slight 2nd harmonic warmth.
 */
static int16_t make_dc_sub_drone(int i) {
    int32_t f1 = sin_fx(i);
    int32_t f2 = sin_fx(i * 2);

    int32_t val = (f1 * 120) + (f2 * 12);
    return (int16_t)clamp16(val);
}

/*
 * Ethereal Swell Body Wavetable:
 * Smooth analog string/chorus body that supports the resonant tail.
 */
static int16_t make_dc_body_swell(int i) {
    int32_t f1 = sin_fx(i);
    int32_t f2 = sin_fx(i * 2);
    int32_t f3 = sin_fx(i * 3);

    int32_t val = (f1 * 96) + (f2 * 26) + (f3 * 10);
    return (int16_t)clamp16(val);
}

static uint32_t s_seq_frame = 0;
static int s_sound_initialized = 0;

void sound_init(void) {
    /* 1. Hold AICA ARM7 sound CPU in reset while SH-4 configures registers */
    *(volatile uint32_t *)0xA0702C00UL |= 1;

    /* 2. Unmute master dry output */
    *(volatile uint16_t *)0xA0702800UL = 0x000F;

    /* 3. Stop and mute all 64 AICA hardware channels */
    for (int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }

    /* 4. Synthesize 256-sample 16-bit single-cycle wavetables in SPU RAM:
          - 0:   DC Bright Crystal Bell Chime
          - 512: DC Low Sub Thump / Drone
          - 1024:DC Ethereal String Pad Swell */
    volatile int16_t *wav_chime = (volatile int16_t *)(AICA_RAM_BASE + 0);
    volatile int16_t *wav_sub   = (volatile int16_t *)(AICA_RAM_BASE + 512);
    volatile int16_t *wav_body  = (volatile int16_t *)(AICA_RAM_BASE + 1024);

    for (int i = 0; i < 256; i++) {
        wav_chime[i] = make_dc_chime(i);
        wav_sub[i]   = make_dc_sub_drone(i);
        wav_body[i]  = make_dc_body_swell(i);
    }

    s_seq_frame = 0;
    s_sound_initialized = 1;
}

void sound_play_note(int ch, int midi_note, int volume, int pan, int wavetable_id) {
    if (ch < 0 || ch >= 64 || !s_sound_initialized) return;

    if (midi_note < 24)  midi_note = 24;
    if (midi_note > 107) midi_note = 107;

    uint32_t pitch = MIDI_PITCH_TABLE[midi_note - 24];
    uint32_t smp_offset = (wavetable_id == 2) ? 1024 : ((wavetable_id == 1) ? 512 : 0);

    /* Stop previous voice */
    AICA_CHN_REG(ch, 0x00) = 0x8000;

    /* Start address & 256-sample loop */
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;

    /* Pitch */
    AICA_CHN_REG(ch, 0x18) = pitch;

    /* ADSR Envelopes tailored to authentic Dreamcast timbre */
    if (wavetable_id == 0) {
        /* Chime: instantaneous acoustic attack, long natural bell decay */
        AICA_CHN_REG(ch, 0x10) = 0x5FC0; /* Fast Attack (AR=31), Sust Level */
        AICA_CHN_REG(ch, 0x14) = 0x3CA8; /* Long Bell Exponential Decay */
    } else if (wavetable_id == 1) {
        /* Deep Sub: punchy transient, sustained sub-bass tail */
        AICA_CHN_REG(ch, 0x10) = 0x1F00;
        AICA_CHN_REG(ch, 0x14) = 0x3C06;
    } else {
        /* Ethereal Pad: gentle swell, long warm release */
        AICA_CHN_REG(ch, 0x10) = 0x0108;
        AICA_CHN_REG(ch, 0x14) = 0x3C86;
    }

    /* AICA Direct Send Level (0 = loudest/0dB, 15 = muted) */
    uint32_t disdl = (volume >= 15) ? 0 : (15 - (volume & 0x0F));
    AICA_CHN_REG(ch, 0x24) = (disdl << 8) | (pan & 0x1F);

    /* Bypass LPF */
    AICA_CHN_REG(ch, 0x28) = 0x0024;

    /* Key On: 16-bit PCM, Loop Enabled */
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
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }
    *(volatile uint16_t *)0xA0702800UL = 0x0000;
    s_sound_initialized = 0;
}

/* -------------------------------------------------------------------------
 * Authentic Sega Dreamcast Boot Jingle Sequence
 * Exactly 8.0 Seconds (480 Frames @ 60 FPS)
 * ------------------------------------------------------------------------- */
void sound_tick(void) {
    if (!s_sound_initialized) return;

    uint32_t tick = s_seq_frame;

    switch (tick) {
        /* 
         * Frame 0: The Swirling Spiral Cascade Begins
         * Rapid upward pentatonic chimes with wide panning bounce
         */
        case 0:
            sound_play_note(0, 84, 13, 0x1A, 0); /* C6  (Left)   */
            break;
        case 5:
            sound_play_note(1, 86, 13, 0x06, 0); /* D6  (Right)  */
            break;
        case 10:
            sound_play_note(2, 91, 14, 0x18, 0); /* G6  (Left)   */
            break;
        case 15:
            sound_play_note(3, 93, 14, 0x08, 0); /* A6  (Right)  */
            break;
        case 20:
            sound_play_note(4, 96, 15, 0x16, 0); /* C7  (Left)   */
            break;
        case 26:
            sound_play_note(5, 98, 15, 0x0A, 0); /* D7  (Right)  */
            break;
        case 33:
            /* High Peak Chime */
            sound_play_note(6, 103, 15, 0x00, 0); /* G7  (Center) */
            break;

        /*
         * Frame 38: The Iconic Low Impact / Sub-Bass Boom
         * The dot drops into the spiral; deep C fundamental activates
         */
        case 38:
            /* Deep Sub-Bass Thump */
            sound_play_note(7, 36, 15, 0x14, 1);  /* C2  (Sub Left)  */
            sound_play_note(8, 36, 15, 0x0C, 1);  /* C2  (Sub Right) */

            /* Warm Harmonizing Low-Mid Body */
            sound_play_note(9, 48, 13, 0x12, 2);  /* C3  (Pad Left)  */
            sound_play_note(10, 55, 12, 0x0E, 2); /* G3  (Pad Right) */

            /* Sparkling Accent Drop */
            sound_play_note(11, 84, 14, 0x00, 0); /* C6  (Accent)    */
            break;

        /* Secondary ambient sparkle during the spiral expansion */
        case 48:
            sound_play_note(12, 96, 12, 0x19, 0); /* C7  (Decaying tail) */
            break;
        case 58:
            sound_play_note(13, 91, 11, 0x07, 0); /* G6  (Decaying tail) */
            break;
        case 70:
            sound_play_note(14, 84, 10, 0x00, 0); /* C6  (Center) */
            break;

        /* 
         * Frame 479: End of 8.0-second sequence 
         */
        case 479:
            sound_stop();
            return;

        default:
            break;
    }

    s_seq_frame++;
}