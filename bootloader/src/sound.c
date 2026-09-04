#include "sound.h"
#include <stdint.h>

/* =========================================================================
 * Y2K ATMOSPHERIC PAD & POLE LEAD AIRBELL SOUND ENGINE
 * =========================================================================
 * Composition:
 *   - Lead Melody: Noisy sine-wave "airbell" pole lead hitting:
 *       D7 -> E5 -> B6 -> B5 -> E6 -> Db7
 *   - Synth Pad: Warm ethereal chord background holding:
 *       A4, B4, D5, and F#5 throughout the sequence.
 *   - Bass & Heartbeat:
 *       Low E4/E2 bass tone + distorted heavy-EQ heartbeat thump pulse.
 *   - Airy Noise:
 *       Filtered cymbal wash fading in halfway with spatial stereo reverb.
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
 * Y2K Atmospheric Waveform Generators
 * ------------------------------------------------------------------------- */

/* 1. Pole Lead / Airbell: Noisy sine-wave with breathy air overtones */
static int16_t make_airbell_lead(int i) {
    int32_t s1 = sin_fx(i);
    int32_t s3 = sin_fx(i * 3);
    int32_t s5 = sin_fx(i * 5);
    /* Subtle high-frequency air flutter */
    int32_t noise = ((i * 37 + 11) % 41) - 20;
    int32_t val = (s1 * 95) + (s3 * 18) + (s5 * 8) + (noise * 12);
    return (int16_t)clamp16(val);
}

/* 2. Warm Atmospheric Synth Pad: Rich mellow chord body */
static int16_t make_warm_pad(int i) {
    int32_t p1 = sin_fx(i);
    int32_t p2 = sin_fx(i * 2);
    int32_t p3 = sin_fx(i * 3);
    int32_t val = (p1 * 88) + (p2 * 26) + (p3 * 10);
    return (int16_t)clamp16(val);
}

/* 3. Distorted Heartbeat Pulse / Sub-Bass Thump */
static int16_t make_heartbeat_thump(int i) {
    int32_t b1 = sin_fx(i);
    /* Saturated non-linear distortion curve */
    int32_t sat = (b1 * 140) / 100;
    if (sat > 200)  sat = 200;
    if (sat < -200) sat = -200;
    int32_t val = (sat * 110) + (sin_fx(i * 2) * 20);
    return (int16_t)clamp16(val);
}

/* 4. Filtered Airy Cymbal Wash / Noise */
static int16_t make_airy_wash(int i) {
    /* Resonant band-filtered white noise approximation */
    int32_t n = ((i * 107 + 73) % 255) - 128;
    int32_t s = sin_fx(i * 4);
    int32_t val = (n * 70) + (s * 35);
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
    volatile int16_t *wav_airbell   = (volatile int16_t *)(AICA_RAM_BASE + 0);
    volatile int16_t *wav_pad       = (volatile int16_t *)(AICA_RAM_BASE + 512);
    volatile int16_t *wav_heartbeat = (volatile int16_t *)(AICA_RAM_BASE + 1024);
    volatile int16_t *wav_wash      = (volatile int16_t *)(AICA_RAM_BASE + 1536);

    for (int i = 0; i < 256; i++) {
        wav_airbell[i]   = make_airbell_lead(i);
        wav_pad[i]       = make_warm_pad(i);
        wav_heartbeat[i] = make_heartbeat_thump(i);
        wav_wash[i]      = make_airy_wash(i);
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
        case 1:  smp_offset = 512;  break; /* Warm Pad */
        case 2:  smp_offset = 1024; break; /* Heartbeat / Bass */
        case 3:  smp_offset = 1536; break; /* Airy Wash */
        default: smp_offset = 0;    break; /* Airbell Pole Lead */
    }

    /* Stop previous voice on channel */
    AICA_CHN_REG(ch, 0x00) = 0x8000;
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;

    /* Hardware ADSR Envelopes */
    if (wavetable_id == 0) {
        /* Pole Lead Airbell: Crisp strike, singing ethereal decay */
        AICA_CHN_REG(ch, 0x10) = 0x0796; /* AR=0x1E, D1R=0x16 */
        AICA_CHN_REG(ch, 0x14) = 0x306E; /* DL=0x0C, D2R=0x03, RR=0x0E */
    } else if (wavetable_id == 1) {
        /* Warm Atmospheric Pad: Swelling organic attack, long sustain */
        AICA_CHN_REG(ch, 0x10) = 0x0302; /* AR=0x0C, D1R=0x02 */
        AICA_CHN_REG(ch, 0x14) = 0x7826; /* DL=0x1E, D2R=0x01, RR=0x06 */
    } else if (wavetable_id == 2) {
        /* Heartbeat Thump / Bass: Fast punchy transient, quick release */
        AICA_CHN_REG(ch, 0x10) = 0x079E; /* AR=0x1E, D1R=0x1E */
        AICA_CHN_REG(ch, 0x14) = 0x104F; /* DL=0x04, D2R=0x04, RR=0x0F */
    } else {
        /* Airy Noise Wash: Slow ethereal swell and delay tail */
        AICA_CHN_REG(ch, 0x10) = 0x0201; /* AR=0x08, D1R=0x01 */
        AICA_CHN_REG(ch, 0x14) = 0x6024; /* DL=0x18, D2R=0x01, RR=0x04 */
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
 * Y2K Sound Sequencer (60 FPS VBlank Tick)
 * ------------------------------------------------------------------------- */
void sound_tick(void) {
    if (!s_sound_initialized) return;

    uint32_t tick = s_seq_frame;

    switch (tick) {
        /* ==============================================================
         * 1. SYNTH PAD: Warm chord holding A4, B4, D5, F#5 throughout
         * ============================================================== */
        case 2:
            /* Warm Pad Chord: A4 (69), B4 (71), D5 (74), F#5 (78) in stereo */
            sound_play_note(0, 69, 11, 0x1C, 1); /* A4  (Left) */
            sound_play_note(1, 71, 10, 0x04, 1); /* B4  (Right) */
            sound_play_note(2, 74, 11, 0x18, 1); /* D5  (Left) */
            sound_play_note(3, 78, 10, 0x08, 1); /* F#5 (Right) */
            break;

        /* ==============================================================
         * 2. BASS & HEARTBEAT: Distorted EQ pulse ("lub-dub" thumps)
         * ============================================================== */
        case 8:
            /* Heartbeat 1 - Pulse A (Low E2/E4 thump) */
            sound_play_note(4, 40, 14, 0x14, 2); /* E2 Heavy Sub Left */
            sound_play_note(5, 64, 13, 0x0C, 2); /* E4 Punch Right */
            break;

        case 16:
            /* Heartbeat 1 - Pulse B */
            sound_play_note(4, 40, 12, 0x10, 2);
            sound_play_note(5, 64, 11, 0x10, 2);
            break;

        /* ==============================================================
         * 3. LEAD MELODY: Noisy sine "airbell" pole lead
         *    Notes: D7 -> E5 -> B6 -> B5 -> E6 -> Db7
         * ============================================================== */
        case 24:
            /* Melody 1: D7 (98) */
            sound_play_note(6, 98, 14, 0x18, 0); /* D7 Left */
            sound_play_note(7, 98, 11, 0x08, 0); /* D7 Spatial Right */
            break;

        case 44:
            /* Melody 2: E5 (76) */
            sound_play_note(8, 76, 13, 0x0A, 0); /* E5 Right */
            sound_play_note(9, 76, 10, 0x16, 0); /* E5 Left */
            break;

        case 64:
            /* Melody 3: B6 (95) */
            sound_play_note(6, 95, 14, 0x1A, 0); /* B6 Left */
            sound_play_note(7, 95, 11, 0x06, 0); /* B6 Right */
            break;

        /* ==============================================================
         * 4. AIRY NOISE & SECOND HEARTBEAT (HALFWAY SWELL)
         * ============================================================== */
        case 70:
            /* Heartbeat 2 - Pulse A */
            sound_play_note(4, 40, 14, 0x14, 2);
            sound_play_note(5, 64, 13, 0x0C, 2);

            /* Filtered airy cymbal wash fades in */
            sound_play_note(10, 72, 12, 0x1E, 3); /* Wash Left */
            sound_play_note(11, 72, 12, 0x02, 3); /* Wash Right */
            break;

        case 78:
            /* Heartbeat 2 - Pulse B */
            sound_play_note(4, 40, 12, 0x10, 2);
            sound_play_note(5, 64, 11, 0x10, 2);
            break;

        case 84:
            /* Melody 4: B5 (83) */
            sound_play_note(8, 83, 13, 0x08, 0); /* B5 Right */
            sound_play_note(9, 83, 10, 0x18, 0); /* B5 Left */
            break;

        case 104:
            /* Melody 5: E6 (88) */
            sound_play_note(6, 88, 14, 0x16, 0); /* E6 Left */
            sound_play_note(7, 88, 11, 0x0A, 0); /* E6 Right */
            break;

        case 124:
            /* Melody 6: Db7 (97) - Resolving Climax Note */
            sound_play_note(8, 97, 15, 0x00, 0); /* Db7 Center Peak */
            sound_play_note(9, 97, 12, 0x14, 0); /* Db7 Stereo Spread */
            break;

        /* ==============================================================
         * 5. CLIMAX BLOOM & SPATIAL REVERB / DELAY TAIL (130+)
         * ============================================================== */
        case 130:
            /* Heartbeat 3 - Climax Pulse & Low E Bass Foundation */
            sound_play_note(4, 40, 15, 0x10, 2); /* Deep E2 Sub */
            sound_play_note(5, 64, 14, 0x10, 2); /* E4 Bass Sustained */

            /* Pad crescendo bloom */
            sound_play_note(0, 69, 13, 0x1E, 1); /* A4 */
            sound_play_note(1, 71, 12, 0x02, 1); /* B4 */
            sound_play_note(2, 74, 13, 0x1A, 1); /* D5 */
            sound_play_note(3, 78, 12, 0x06, 1); /* F#5 */
            break;

        /* Spatial diffuse echoes of resolving Db7 / E6 */
        case 155:
            sound_play_note(6, 97, 8, 0x0E, 0); /* Db7 Echo Right */
            sound_play_note(7, 88, 7, 0x12, 0); /* E6 Echo Left */
            break;

        case 180:
            sound_play_note(8, 97, 5, 0x14, 0); /* Db7 Diffuse Tail */
            break;

        default:
            break;
    }

    s_seq_frame++;
}
