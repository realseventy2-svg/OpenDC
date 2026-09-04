#include "sound.h"
#include <stdint.h>

/* =========================================================================
 * SERENE & SOOTHING SEGA DREAMCAST STARTUP SOUND ENGINE
 * =========================================================================
 * Sound Design:
 *   - Velvet Celesta Bell:
 *     Warm acoustic singing fundamental (AICA 16-bit PCM),
 *     smooth felt strike (~7ms rise, zero pop/click), long singing acoustic decay.
 *   - Lush Celestial String & Choral Pad:
 *     Warm, wide analog stereo chorus that breathes gently in the background.
 *   - Deep Acoustic Sub-Bass Foundation:
 *     Rounded warm resonance grounding the calm atmosphere.
 *   - Silky Stardust Air Bloom:
 *     Soft atmospheric air shimmer resolving into the BIOS transition.
 *   - 1.5-Second Master Fade-Out:
 *     Smooth 90-frame gradual fade-out across T = 2.5s..4.0s (frames 150..240).
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
 * High-Fidelity Soothing Acoustic Waveform Generators
 * ------------------------------------------------------------------------- */

/* 1. Velvet Celesta Bell: Silky, round fundamental with warm acoustic body */
static int16_t make_velvet_celesta_bell(int i) {
    int32_t s1 = sin_fx(i);
    int32_t s2 = sin_fx(i * 2);
    int32_t s3 = sin_fx(i * 3);
    /* 98% pure singing fundamental with subtle acoustic overtone */
    int32_t val = (s1 * 98) + (s2 * 14) + (s3 * 4);
    return (int16_t)clamp16(val);
}

/* 2. Serene Ambient Strings Pad: Warm, lush choral-analog ensemble */
static int16_t make_serene_ambient_pad(int i) {
    int32_t p1 = sin_fx(i);
    int32_t p2 = sin_fx(i * 2);
    int32_t p3 = sin_fx(i * 3);
    int32_t val = (p1 * 92) + (p2 * 18) + (p3 * 6);
    return (int16_t)clamp16(val);
}

/* 3. Warm Acoustic Sub-Bass: Deep, rounded cinematic resonance */
static int16_t make_warm_acoustic_bass(int i) {
    int32_t b1 = sin_fx(i);
    int32_t b2 = sin_fx(i * 2);
    int32_t val = (b1 * 100) + (b2 * 20);
    return (int16_t)clamp16(val);
}

/* 4. Silky Stardust Air Bloom: Soft, diffused atmospheric sheen */
static int16_t make_silky_air_bloom(int i) {
    int32_t s1 = sin_fx(i * 2);
    int32_t s2 = sin_fx(i * 4);
    int32_t val = (s1 * 60) + (s2 * 25);
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

    /* 2. Set master dry output volume to comfortable level (0x000E) */
    *(volatile uint16_t *)0xA0702800UL = 0x000E;

    /* 3. Silence all 64 channels */
    for (int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }

    /* 4. Synthesize 4 16-bit PCM wavetables in SPU RAM */
    volatile int16_t *wav_bell    = (volatile int16_t *)(AICA_RAM_BASE + 0);
    volatile int16_t *wav_pad     = (volatile int16_t *)(AICA_RAM_BASE + 512);
    volatile int16_t *wav_bass    = (volatile int16_t *)(AICA_RAM_BASE + 1024);
    volatile int16_t *wav_shimmer = (volatile int16_t *)(AICA_RAM_BASE + 1536);

    for (int i = 0; i < 256; i++) {
        wav_bell[i]    = make_velvet_celesta_bell(i);
        wav_pad[i]     = make_serene_ambient_pad(i);
        wav_bass[i]    = make_warm_acoustic_bass(i);
        wav_shimmer[i] = make_silky_air_bloom(i);
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
        case 1:  smp_offset = 512;  break; /* Serene Ambient Pad */
        case 2:  smp_offset = 1024; break; /* Warm Acoustic Bass */
        case 3:  smp_offset = 1536; break; /* Silky Stardust Air Bloom */
        default: smp_offset = 0;    break; /* Velvet Celesta Bell */
    }

    /* Stop previous voice on channel */
    AICA_CHN_REG(ch, 0x00) = 0x8000;
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;

    /* Hardware ADSR Envelopes:
     * Reg 0x10: (D2R << 11) | (D1R << 6) | AR
     * Reg 0x14: (KRS << 10) | (DL << 5)  | RR
     */
    if (wavetable_id == 0) {
        /* Velvet Celesta Bell: Smooth felt rise (AR=22 ~6.8ms), singing decay (D1R=6), long sustain */
        AICA_CHN_REG(ch, 0x10) = (1 << 11) | (6 << 6) | 22; /* D2R=1, D1R=6, AR=22 */
        AICA_CHN_REG(ch, 0x14) = (0 << 10) | (0 << 5) | 16; /* KRS=0, DL=0, RR=16 */
    } else if (wavetable_id == 1) {
        /* Serene Ambient Pad: Gentle breathing rise (AR=18 ~25ms), warm sustained body */
        AICA_CHN_REG(ch, 0x10) = (1 << 11) | (2 << 6) | 18; /* D2R=1, D1R=2, AR=18 */
        AICA_CHN_REG(ch, 0x14) = (0 << 10) | (0 << 5) | 12; /* KRS=0, DL=0, RR=12 */
    } else if (wavetable_id == 2) {
        /* Warm Acoustic Bass: Rounded deep rise (AR=20 ~12ms), solid warm sustain */
        AICA_CHN_REG(ch, 0x10) = (1 << 11) | (3 << 6) | 20; /* D2R=1, D1R=3, AR=20 */
        AICA_CHN_REG(ch, 0x14) = (0 << 10) | (0 << 5) | 14; /* KRS=0, DL=0, RR=14 */
    } else {
        /* Silky Stardust Air Bloom: Slow dreamy rise (AR=16 ~50ms) */
        AICA_CHN_REG(ch, 0x10) = (1 << 11) | (1 << 6) | 16; /* D2R=1, D1R=1, AR=16 */
        AICA_CHN_REG(ch, 0x14) = (0 << 10) | (0 << 5) | 10; /* KRS=0, DL=0, RR=10 */
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
 * Serene Dreamcast Startup Music Sequencer (60 FPS VBlank Tick / 4.0 Seconds)
 * -------------------------------------------------------------------------
 * Melody Pacing (seconds):
 *   - T = 0.0s (Frame 2):   Ambient ocean swell & sub-bass foundation
 *   - T = 0.4s (Frame 24):  Chime 1 (E4) - Smooth felt bell
 *   - T = 1.0s (Frame 60):  Chime 2 (G#4) - Serene melodic lift
 *   - T = 1.6s (Frame 96):  Chime 3 (B4) - Radiant singing bell
 *   - T = 2.2s (Frame 132): Climax Resolution Bell & Chord Bloom (E5)
 *   - T = 2.5s..4.0s (Frames 150..240 / 1.5s): Smooth 90-frame gradual fade-out
 * ------------------------------------------------------------------------- */
void sound_tick(void) {
    if (!s_sound_initialized) return;

    uint32_t tick = s_seq_frame;

    switch (tick) {
        /* ==============================================================
         * 1. INTRO (T = 0.03s / Frame 2): Soft Ambient Ocean & Strings
         *    Lush, peaceful E Major 9 chord (E2 + E3 + B3 + G#4)
         * ============================================================== */
        case 2:
            /* Warm Sub-Bass Foundation (E2 = MIDI 40) - Mellow level 9 */
            sound_play_note(0, 40, 9, 0x00, 2);

            /* Serene Ambient Strings in Stereo - Soft level 8 */
            sound_play_note(1, 52, 8, 0x1E, 1); /* E3  (Left) */
            sound_play_note(2, 59, 8, 0x0E, 1); /* B3  (Right) */
            sound_play_note(3, 68, 8, 0x1A, 1); /* G#4 (Left) */
            sound_play_note(4, 75, 7, 0x0A, 1); /* D#5 (Right) */
            break;

        /* ==============================================================
         * 2. SOOTHING CHIME 1 (T = 0.40s / Frame 24):
         *    First peaceful felt bell (E4 = MIDI 64) ringing out in space
         * ============================================================== */
        case 24:
            sound_play_note(5, 64, 11, 0x18, 0); /* Left-Center Warm Chime (level 11) */
            sound_play_note(6, 64, 8,  0x08, 0); /* Subtle Stereo Ambient Echo (level 8) */
            break;

        /* ==============================================================
         * 3. SOOTHING CHIME 2 (T = 1.00s / Frame 60):
         *    Second contemplative bell (G#4 = MIDI 68) singing warmly
         * ============================================================== */
        case 60:
            sound_play_note(7, 68, 11, 0x08, 0); /* Right-Center Warm Chime (level 11) */
            sound_play_note(8, 68, 8,  0x18, 0); /* Subtle Stereo Ambient Echo (level 8) */
            break;

        /* ==============================================================
         * 4. SOOTHING CHIME 3 (T = 1.60s / Frame 96):
         *    Third singing bell (B4 = MIDI 71) ascending smoothly
         * ============================================================== */
        case 96:
            sound_play_note(5, 71, 11, 0x16, 0); /* Left-Center Warm Chime (level 11) */
            sound_play_note(6, 71, 8,  0x0A, 0); /* Subtle Stereo Ambient Echo (level 8) */
            break;

        /* ==============================================================
         * 5. SOOTHING RESOLUTION CHIME & CLIMAX BLOOM (T = 2.20s / Frame 132):
         *    Majestic, calm resolution bell (E5 = MIDI 76)
         *    accompanied by radiant warm major 9 chord expansion
         * ============================================================== */
        case 132:
            /* Peaceful Resolution Bell (E5) - Mellow level 11 */
            sound_play_note(9,  76, 11, 0x00, 0); /* Center Melody */
            sound_play_note(10, 76, 9,  0x1E, 0); /* Soft Left Chorus */
            sound_play_note(11, 76, 9,  0x0E, 0); /* Soft Right Chorus */

            /* Silky Stardust Air Bloom in Stereo - Subtle level 7 */
            sound_play_note(12, 68, 7, 0x1E, 3); /* G#4 air shimmer */
            sound_play_note(13, 68, 7, 0x0E, 3); /* G#4 air shimmer */

            /* Ambient Strings Full Warmth Bloom */
            sound_play_note(1, 52, 9, 0x1E, 1); /* E3 */
            sound_play_note(2, 64, 9, 0x00, 1); /* E4 */
            sound_play_note(3, 71, 9, 0x0E, 1); /* B4 */
            sound_play_note(4, 76, 8, 0x1A, 1); /* E5 */
            break;

        default:
            break;
    }

    /* ==============================================================
     * 6. EXTENDED 1.5-SECOND MASTER FADE-OUT (T = 2.5s..4.0s / Frames 150..240):
     *    Very slow, gradual 90-frame perceptual decrescendo (1.5 full seconds)
     *    giving the resolution chord ample time to bloom and gently dissolve
     *    into the BIOS dashboard.
     * ============================================================== */
    if (tick >= 150 && tick <= 240) {
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
        int idx = (int)tick - 150;
        if (idx < 0) idx = 0;
        if (idx > 90) idx = 90;
        *(volatile uint16_t *)0xA0702800UL = s_fade_curve[idx];
    }

    s_seq_frame++;
}
