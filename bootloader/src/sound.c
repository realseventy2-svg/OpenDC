#include "sound.h"
#include <stdint.h>

/* =========================================================================
   AUTHENTIC SEGA DREAMCAST STARTUP ACOUSTIC SOUND ENGINE
   =========================================================================
   Composed by Ryuichi Sakamoto (1998)
   Faithfully reconstructed on direct Yamaha AICA 64-Channel SPU hardware.

   Structure:
     1. Swirl Drawing Phase (0 - 2.0s):
        Delicate 8-note acoustic chime arpeggio:
        B5 -> D6 -> G6 -> F#6 -> D6 -> B5 -> A5 -> D5
        Accompanied by a swelling warm Gmaj7 -> Dsus4 acoustic pad in stereo.

     2. Swirl Resolution / Logo Drop Phase (2.2s - 4.5s):
        Iconic deep orchestral sub-bass drop (D1 + D2),
        Blooming Dmaj9 acoustic string/Rhodes chord (A3, D4, F#4, C#5, E5),
        and resolving high glass chime accord (F#6 + A6) with spatial echoes.

   Timbres are synthesized via 4 multi-harmonic 16-bit PCM wavetables in
   SPU RAM:
     - Table 0: Acoustic Celesta / Music Box / Glockenspiel (Rich Overtones)
     - Table 1: Warm Acoustic Rhodes / Electric Piano
     - Table 2: Lush Orchestral String Ensemble Pad
     - Table 3: Deep Resonant Acoustic Sub-Bass
   ========================================================================= */

/* -------------------------------------------------------------------------
 * MIDI (21..108) -> Yamaha AICA Hardware Pitch Register Table
 * Format: (OCT << 11) | FNS
 * ------------------------------------------------------------------------- */
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

/* 8.8 Fixed-Point Sine Quarter-Wave Table */
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
 * Multi-Harmonic Acoustic Physical Modeling Waveforms
 * ------------------------------------------------------------------------- */

/* 1. Acoustic Celesta / Glockenspiel / Music Box (Harmonics f1, f2, f3, f4, f5) */
static int16_t make_acoustic_chime(int i) {
    int32_t f1 = sin_fx(i);
    int32_t f2 = sin_fx(i * 2);
    int32_t f3 = sin_fx(i * 3);
    int32_t f4 = sin_fx(i * 4);
    int32_t f5 = sin_fx(i * 5);
    int32_t val = (f1 * 84) + (f2 * 26) + (f3 * 12) + (f4 * 5) + (f5 * 2);
    return (int16_t)clamp16(val);
}

/* 2. Warm Acoustic Rhodes Electric Piano / Mellow Body */
static int16_t make_rhodes_body(int i) {
    int32_t r1 = sin_fx(i);
    int32_t r2 = sin_fx(i * 2);
    int32_t r3 = sin_fx(i * 3);
    int32_t val = (r1 * 92) + (r2 * 22) + (r3 * 10);
    return (int16_t)clamp16(val);
}

/* 3. Lush Orchestral String Ensemble Pad */
static int16_t make_orchestral_pad(int i) {
    int32_t s1 = sin_fx(i);
    int32_t s2 = sin_fx(i * 2);
    int32_t s3 = sin_fx(i * 3);
    int32_t s4 = sin_fx(i * 4);
    int32_t val = (s1 * 75) + (s2 * 32) + (s3 * 16) + (s4 * 8);
    return (int16_t)clamp16(val);
}

/* 4. Deep Resonant Acoustic Sub-Bass */
static int16_t make_sub_bass(int i) {
    int32_t b1 = sin_fx(i);
    int32_t b2 = sin_fx(i * 2);
    int32_t val = (b1 * 110) + (b2 * 16);
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

    /* 4. Synthesize 4 acoustic 256-sample 16-bit PCM wavetables in SPU RAM */
    volatile int16_t *wav_chime  = (volatile int16_t *)(AICA_RAM_BASE + 0);
    volatile int16_t *wav_rhodes = (volatile int16_t *)(AICA_RAM_BASE + 512);
    volatile int16_t *wav_string = (volatile int16_t *)(AICA_RAM_BASE + 1024);
    volatile int16_t *wav_sub    = (volatile int16_t *)(AICA_RAM_BASE + 1536);

    for (int i = 0; i < 256; i++) {
        wav_chime[i]  = make_acoustic_chime(i);
        wav_rhodes[i] = make_rhodes_body(i);
        wav_string[i] = make_orchestral_pad(i);
        wav_sub[i]    = make_sub_bass(i);
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
        case 1:  smp_offset = 512;  break; /* Rhodes */
        case 2:  smp_offset = 1024; break; /* Strings */
        case 3:  smp_offset = 1536; break; /* Sub-Bass */
        default: smp_offset = 0;    break; /* Chime */
    }

    /* Stop previous voice on channel */
    AICA_CHN_REG(ch, 0x00) = 0x8000;
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;

    /* Acoustic Hardware ADSR Configuration */
    if (wavetable_id == 0) {
        /* Acoustic Chime / Bell: Fast strike, singing long acoustic decay */
        AICA_CHN_REG(ch, 0x10) = 0x0794; /* AR=0x1E, D1R=0x14 */
        AICA_CHN_REG(ch, 0x14) = 0x206E; /* DL=0x08, D2R=0x03, RR=0x0E */
    } else if (wavetable_id == 1) {
        /* Acoustic Rhodes: Warm hammer attack, organic sustain */
        AICA_CHN_REG(ch, 0x10) = 0x0710; /* AR=0x1C, D1R=0x10 */
        AICA_CHN_REG(ch, 0x14) = 0x404C; /* DL=0x10, D2R=0x02, RR=0x0C */
    } else if (wavetable_id == 2) {
        /* Orchestral String Pad: Swelling lush attack, warm body */
        AICA_CHN_REG(ch, 0x10) = 0x0304; /* AR=0x0C, D1R=0x04 */
        AICA_CHN_REG(ch, 0x14) = 0x7828; /* DL=0x1E, D2R=0x01, RR=0x08 */
    } else {
        /* Sub-Bass: Solid low impact, deep resonance */
        AICA_CHN_REG(ch, 0x10) = 0x0786; /* AR=0x1E, D1R=0x06 */
        AICA_CHN_REG(ch, 0x14) = 0x704A; /* DL=0x1C, D2R=0x02, RR=0x0A */
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
 * Canonical Sega Dreamcast Boot Audio Sequencer (60 FPS VBlank Tick)
 * ------------------------------------------------------------------------- */
void sound_tick(void) {
    if (!s_sound_initialized) return;

    uint32_t tick = s_seq_frame;

    switch (tick) {
        /* ==============================================================
         * PHASE 1: THE ICONIC DRAWING OF THE SEGA DREAMCAST SPIRAL (0..120)
         * Delicate acoustic chime arpeggio: B5 -> D6 -> G6 -> F#6 -> D6 -> B5 -> A5 -> D5
         * ============================================================== */
        case 8:
            /* Note 1: B5 (71) */
            sound_play_note(0, 71, 13, 0x1A, 0); /* Left */
            sound_play_note(1, 71, 10, 0x06, 0); /* Stereo Chorus Right */
            /* Background Pad: G3 + D4 */
            sound_play_note(2, 55,  9, 0x1C, 2); /* G3 String Left */
            sound_play_note(3, 62,  9, 0x04, 2); /* D4 String Right */
            break;

        case 22:
            /* Note 2: D6 (74) */
            sound_play_note(4, 74, 13, 0x08, 0); /* Right */
            sound_play_note(5, 74, 10, 0x18, 0); /* Stereo Chorus Left */
            break;

        case 36:
            /* Note 3: G6 (79) */
            sound_play_note(6, 79, 14, 0x00, 0); /* Center Peak */
            break;

        case 50:
            /* Note 4: F#6 (78) */
            sound_play_note(0, 78, 13, 0x18, 0); /* Left */
            sound_play_note(1, 78, 10, 0x08, 0); /* Right */
            break;

        case 64:
            /* Note 5: D6 (74) */
            sound_play_note(4, 74, 12, 0x0A, 0); /* Right */
            /* Pad transition: A3 + D4 */
            sound_play_note(2, 57,  9, 0x1A, 2); /* A3 String Left */
            sound_play_note(3, 62,  9, 0x06, 2); /* D4 String Right */
            break;

        case 78:
            /* Note 6: B5 (71) */
            sound_play_note(5, 71, 12, 0x00, 0); /* Center */
            break;

        case 92:
            /* Note 7: A5 (69) */
            sound_play_note(0, 69, 13, 0x16, 0); /* Left */
            sound_play_note(1, 69, 10, 0x0A, 0); /* Right */
            break;

        case 106:
            /* Note 8: D5 (62) - Resolving Arpeggio Anchor */
            sound_play_note(4, 62, 13, 0x00, 0); /* Center */
            sound_play_note(5, 62, 11, 0x14, 1); /* Rhodes Warmth */
            break;

        /* ==============================================================
         * PHASE 2: LOGO DROP CLIMAX & BREATHTAKING DMAJ9 CHORD RESOLUTION (130)
         * Deep orchestral sub drop + Wide Dmaj9 bloom + High chime accord
         * ============================================================== */
        case 130:
            /* 1. Deep Orchestral Sub-Bass Drop (D1 + D2) */
            sound_play_note(6, 26, 15, 0x16, 3); /* D1 Sub Left */
            sound_play_note(7, 38, 14, 0x0A, 3); /* D2 Sub Right */

            /* 2. Warm Dmaj9 Acoustic Rhodes Body */
            sound_play_note(8, 45, 12, 0x18, 1); /* A2 Rhodes Left */
            sound_play_note(9, 54, 12, 0x08, 1); /* F#3 Rhodes Right */

            /* 3. Wide Orchestral String Ensemble Bloom (A3, D4, C#5, E5) */
            sound_play_note(10, 57, 12, 0x1E, 2); /* A3 String Far Left */
            sound_play_note(11, 62, 12, 0x02, 2); /* D4 String Far Right */
            sound_play_note(12, 73, 11, 0x18, 2); /* C#5 String Left */
            sound_play_note(13, 76, 11, 0x08, 2); /* E5 String Right */

            /* 4. High Celestial Chime Accord (F#6 + A6) */
            sound_play_note(14, 90, 14, 0x14, 0); /* F#6 Chime Left */
            sound_play_note(15, 93, 14, 0x0C, 0); /* A6 Chime Right */
            break;

        /* Phase 3: Spatial Diffuse Acoustic Reverb Echoes */
        case 160:
            sound_play_note(0, 90, 8, 0x0E, 0); /* F#6 Echo Right */
            sound_play_note(1, 93, 8, 0x12, 0); /* A6 Echo Left */
            break;

        case 190:
            sound_play_note(4, 93, 5, 0x00, 0); /* A6 Diffuse Center Tail */
            break;

        default:
            break;
    }

    s_seq_frame++;
}
