#include "sound.h"
#include <stdint.h>

/* =========================================================================
 * FRUTIGER AERO CRYSTALLINE GLASS & ETHEREAL AMBIENT SOUND ENGINE
 * =========================================================================
 * Sound Design:
 *   - Crystal Chime Cascade: Sparkling glass bell strikes with crystalline
 *     harmonics ascending in E Major 9 (E5 -> G#5 -> B5 -> D#6 -> F#6 -> G#6 -> E7).
 *   - Aero Glass Pad: Lush, warm, euphoric atmospheric chord background
 *     swelling with E3, B3, F#4, G#4, B4, D#5.
 *   - Liquid Glass Droplet: Resonant, organic bubble-drop chime transient.
 *   - Shimmer Wash: Filtered airy high-frequency atmospheric sheen.
 *
 * Direct Yamaha AICA 64-Channel Hardware Synthesizer.
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

/* -------------------------------------------------------------------------
 * Frutiger Aero Waveform Generators
 * ------------------------------------------------------------------------- */

/* 1. Crystal Glass Chime: Pure bell fundamental with ringing glass partials */
static int16_t make_crystal_glass_chime(int i) {
    int32_t s1 = sin_fx(i);
    int32_t s3 = sin_fx(i * 3);
    int32_t s6 = sin_fx(i * 6);
    int32_t s9 = sin_fx(i * 9);
    /* Glass metallic bell harmonics */
    int32_t val = (s1 * 90) + (s3 * 28) + (s6 * 14) + (s9 * 6);
    return (int16_t)clamp16(val);
}

/* 2. Aero Glass Pad: Warm, lush, airy choral-pad body */
static int16_t make_aero_glass_pad(int i) {
    int32_t p1 = sin_fx(i);
    int32_t p2 = sin_fx(i * 2);
    int32_t p4 = sin_fx(i * 4);
    int32_t val = (p1 * 92) + (p2 * 24) + (p4 * 12);
    return (int16_t)clamp16(val);
}

/* 3. Liquid Waterdrop / Glass Droplet Pluck */
static int16_t make_waterdrop_pluck(int i) {
    int32_t w1 = sin_fx(i);
    int32_t w2 = sin_fx(i * 2);
    int32_t w5 = sin_fx(i * 5);
    int32_t val = (w1 * 95) + (w2 * 20) + (w5 * 8);
    return (int16_t)clamp16(val);
}

/* 4. Shimmer Wash: Filtered airy high-frequency atmospheric sheen */
static int16_t make_aero_shimmer_wash(int i) {
    int32_t n = ((i * 131 + 47) % 255) - 128;
    int32_t s1 = sin_fx(i * 8);
    int32_t s2 = sin_fx(i * 12);
    int32_t val = (n * 45) + (s1 * 30) + (s2 * 18);
    return (int16_t)clamp16(val);
}

static uint32_t s_seq_frame = 0;
static int s_sound_initialized = 0;

/* -------------------------------------------------------------------------
 * Hardware SPU Initialization
 * ------------------------------------------------------------------------- */
void sound_init(void) {
    /* 1. Hold ARM CPU in reset for direct SH-4 hardware sound synthesis */
    *(volatile uint32_t *)0xA0702C00UL |= 1;

    /* 2. Unmute master dry output volume */
    *(volatile uint16_t *)0xA0702800UL = 0x000F;

    /* 3. Silence all 64 channels */
    for (int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }

    /* 4. Synthesize 4 16-bit PCM wavetables in SPU RAM */
    volatile int16_t *wav_chime     = (volatile int16_t *)(AICA_RAM_BASE + 0);
    volatile int16_t *wav_pad       = (volatile int16_t *)(AICA_RAM_BASE + 512);
    volatile int16_t *wav_waterdrop = (volatile int16_t *)(AICA_RAM_BASE + 1024);
    volatile int16_t *wav_shimmer   = (volatile int16_t *)(AICA_RAM_BASE + 1536);

    for (int i = 0; i < 256; i++) {
        wav_chime[i]     = make_crystal_glass_chime(i);
        wav_pad[i]       = make_aero_glass_pad(i);
        wav_waterdrop[i] = make_waterdrop_pluck(i);
        wav_shimmer[i]   = make_aero_shimmer_wash(i);
    }

    s_seq_frame = 0;
    s_sound_initialized = 1;
}

/* -------------------------------------------------------------------------
 * Note Playback with Hardware ADSR Envelopes
 * ------------------------------------------------------------------------- */
void sound_play_note(int ch, int midi_note, int volume, int pan, int wavetable_id) {
    if (ch < 0 || ch >= 64 || !s_sound_initialized) return;

    if (midi_note < 21) midi_note = 21;
    if (midi_note > 108) midi_note = 108;

    uint32_t pitch = MIDI_PITCH_TABLE[midi_note - 21];
    uint32_t smp_offset;

    switch (wavetable_id) {
        case 1:  smp_offset = 512;  break; /* Aero Glass Pad */
        case 2:  smp_offset = 1024; break; /* Waterdrop Pluck */
        case 3:  smp_offset = 1536; break; /* Shimmer Wash */
        default: smp_offset = 0;    break; /* Crystal Glass Chime */
    }

    /* Stop previous voice on channel */
    AICA_CHN_REG(ch, 0x00) = 0x8000;
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;

    /* Hardware ADSR Envelopes */
    if (wavetable_id == 0) {
        /* Crystal Glass Chime: Instant strike, long singing crystalline sustain */
        AICA_CHN_REG(ch, 0x10) = 0x079C; /* AR=0x1E, D1R=0x1C */
        AICA_CHN_REG(ch, 0x14) = 0x205E; /* DL=0x08, D2R=0x02, RR=0x0E */
    } else if (wavetable_id == 1) {
        /* Aero Glass Pad: Gentle swelling organic attack, warm sustain */
        AICA_CHN_REG(ch, 0x10) = 0x0282; /* AR=0x0A, D1R=0x02 */
        AICA_CHN_REG(ch, 0x14) = 0x7828; /* DL=0x1E, D2R=0x01, RR=0x08 */
    } else if (wavetable_id == 2) {
        /* Liquid Waterdrop: Quick rounded attack, resonant decay */
        AICA_CHN_REG(ch, 0x10) = 0x0796; /* AR=0x1E, D1R=0x16 */
        AICA_CHN_REG(ch, 0x14) = 0x183E; /* DL=0x06, D2R=0x03, RR=0x0E */
    } else {
        /* Shimmer Wash: Soft airy fade-in with diffuse delay tail */
        AICA_CHN_REG(ch, 0x10) = 0x0201; /* AR=0x08, D1R=0x01 */
        AICA_CHN_REG(ch, 0x14) = 0x6026; /* DL=0x18, D2R=0x01, RR=0x06 */
    }

    AICA_CHN_REG(ch, 0x18) = pitch;
    AICA_CHN_REG(ch, 0x24) = ((uint32_t)(volume & 0x0F) << 8) | (pan & 0x1F);
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
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }
    *(volatile uint16_t *)0xA0702800UL = 0x0000;
    s_sound_initialized = 0;
}

/* -------------------------------------------------------------------------
 * Frutiger Aero Boot Sound Sequencer (60 FPS VBlank Tick)
 * ------------------------------------------------------------------------- */
void sound_tick(void) {
    if (!s_sound_initialized) return;

    uint32_t tick = s_seq_frame;

    switch (tick) {
        /* ==============================================================
         * 1. INTRO: Warm Ambient Aero Pad Swell & Sub-Bass Foundation
         *    Chord: E Major 9 (E3 + B3 + F#4 + G#4 + B4 + D#5)
         * ============================================================== */
        case 2:
            /* Sub Bass Foundation (E2 = MIDI 40) */
            sound_play_note(0, 40, 13, 0x10, 2); /* Deep Liquid Sub Center */

            /* Ethereal Pad Swell in Stereo */
            sound_play_note(1, 52, 10, 0x1A, 1); /* E3  (Left) */
            sound_play_note(2, 59, 10, 0x06, 1); /* B3  (Right) */
            sound_play_note(3, 66, 11, 0x18, 1); /* F#4 (Left) */
            sound_play_note(4, 68, 11, 0x08, 1); /* G#4 (Right) */
            sound_play_note(5, 75, 10, 0x14, 1); /* D#5 (Left) */
            break;

        /* ==============================================================
         * 2. CRYSTALLINE GLASS CHIME CASCADE (Ascending E Maj9 Arpeggio)
         * ============================================================== */
        case 12:
            /* Note 1: E5 (76) - Glass Strike Left */
            sound_play_note(6, 76, 14, 0x1A, 0);
            sound_play_note(7, 76, 10, 0x06, 0);
            /* Waterdrop accent */
            sound_play_note(8, 76, 11, 0x14, 2);
            break;

        case 20:
            /* Note 2: G#5 (80) - Glass Strike Right */
            sound_play_note(9,  80, 14, 0x06, 0);
            sound_play_note(10, 80, 10, 0x1A, 0);
            break;

        case 28:
            /* Note 3: B5 (83) - Crystalline Bell Center-Left */
            sound_play_note(11, 83, 14, 0x16, 0);
            sound_play_note(12, 83, 11, 0x0A, 0);
            /* Waterdrop accent */
            sound_play_note(8, 83, 11, 0x0C, 2);
            break;

        case 36:
            /* Note 4: D#6 (87) - High Chime Center-Right */
            sound_play_note(13, 87, 14, 0x08, 0);
            sound_play_note(14, 87, 11, 0x18, 0);
            break;

        case 44:
            /* Note 5: F#6 (90) - Shimmering Chime Left */
            sound_play_note(6, 90, 14, 0x1C, 0);
            sound_play_note(7, 90, 11, 0x04, 0);
            break;

        case 52:
            /* Note 6: G#6 (92) - Top Glass Peak Right */
            sound_play_note(9,  92, 15, 0x04, 0);
            sound_play_note(10, 92, 12, 0x1C, 0);
            break;

        /* ==============================================================
         * 3. CLIMAX BLOOM: High E7 Glass Harmonic + Full Chord Radiance
         * ============================================================== */
        case 60:
            /* Note 7 (Climax Peak): E7 (100) - Pure Ringing Glass Bell */
            sound_play_note(11, 100, 15, 0x10, 0); /* Center Climax */
            sound_play_note(12, 100, 13, 0x1E, 0); /* Left Spread */
            sound_play_note(13, 100, 13, 0x02, 0); /* Right Spread */

            /* Shimmer wash air bloom */
            sound_play_note(14, 80, 12, 0x1C, 3);
            sound_play_note(15, 80, 12, 0x04, 3);

            /* Pad Crescendo Brightness */
            sound_play_note(1, 52, 12, 0x1C, 1); /* E3 */
            sound_play_note(2, 64, 12, 0x10, 1); /* E4 */
            sound_play_note(3, 71, 13, 0x04, 1); /* B4 */
            sound_play_note(4, 76, 13, 0x18, 1); /* E5 */
            sound_play_note(5, 83, 12, 0x08, 1); /* B5 */
            break;

        /* ==============================================================
         * 4. SPATIAL DIFFUSE ECHOES & ETHEREAL DECAY TAIL
         * ============================================================== */
        case 85:
            /* Glass harmonic echo 1 (G#6 right / E6 left) */
            sound_play_note(6, 92, 9, 0x06, 0);
            sound_play_note(7, 88, 8, 0x1A, 0);
            break;

        case 110:
            /* Glass harmonic echo 2 (B5 center-right) */
            sound_play_note(8, 83, 7, 0x0A, 0);
            sound_play_note(9, 76, 6, 0x16, 0);
            break;

        case 140:
            /* Final delicate crystal shimmer tail */
            sound_play_note(10, 100, 5, 0x12, 0);
            break;

        default:
            break;
    }

    s_seq_frame++;
}
