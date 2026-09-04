#include "sound.h"
#include <stdint.h>

/*
 * Dreamcast-style boot jingle - homebrew tribute
 * ---------------------------------------------------------------------
 * NOTE: this is an original synthesis inspired by the shape of Sega's
 * startup chime (rising spiral -> low impact -> shimmering tail). It
 * is not, and doesn't attempt to be, a sample-accurate clone of Sega's
 * actual copyrighted jingle - that's proprietary audio, not something
 * to reverse-engineer byte-for-byte. Everything below is synthesized
 * from scratch on your AICA wavetables.
 *
 * What's new in this revision:
 *
 *   1. Pseudo-reverb via early reflections + decay tail.
 *      The AICA's actual hardware reverb lives in its onboard DSP,
 *      which requires uploading a full microprogram (MPRO/coefficient
 *      RAM) to the ARM7 side - well beyond a single voice function.
 *      The classic homebrew workaround (used on plenty of real
 *      Saturn/DC-era titles) is what's here instead: every important
 *      hit gets 1-2 quiet, slightly detuned/pan-shifted repeats on a
 *      spare channel a few frames later, plus the long decaying tail
 *      that was already in your case 48/58/70. Close together those
 *      read as "room", not as discrete echoes.
 *
 *   2. A 4th instrument: a thin, bright "shimmer" layer, distinct
 *      from the main chime, used only for the secondary sparkle so
 *      the tail doesn't just sound like a quieter copy of the lead.
 *
 *   3. Real hardware LFO (AICA register 0x1C) instead of a static
 *      tone: a slow pitch vibrato on the peak chime, and a slow
 *      amplitude throb on the sub-bass impact.
 *
 * Sequence length is unchanged: exactly 480 frames @ 60 FPS = 8.0s,
 * one-shot, ending in sound_stop() at frame 479.
 * ------------------------------------------------------------------------- */

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

static int32_t clampi(int32_t value, int32_t lo, int32_t hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/*
 * Bright, crystalline tine with sharp bell overtone structure
 * (1st, 3rd, 5th, and 8th). Main lead voice for the spiral.
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
 * Deep sub thump / sub-bass drone: clean fundamental, slight
 * 2nd-harmonic warmth.
 */
static int16_t make_dc_sub_drone(int i) {
    int32_t f1 = sin_fx(i);
    int32_t f2 = sin_fx(i * 2);

    int32_t val = (f1 * 120) + (f2 * 12);
    return (int16_t)clamp16(val);
}

/*
 * Ethereal swell body: smooth analog string/chorus pad supporting
 * the resonant tail.
 */
static int16_t make_dc_body_swell(int i) {
    int32_t f1 = sin_fx(i);
    int32_t f2 = sin_fx(i * 2);
    int32_t f3 = sin_fx(i * 3);

    int32_t val = (f1 * 96) + (f2 * 26) + (f3 * 10);
    return (int16_t)clamp16(val);
}

/*
 * Shimmer: a thin, higher, quieter instrument used only for the
 * secondary sparkle/tail so it reads as a *different* voice from the
 * lead chime, not just a quieter copy of it. Weighted toward the
 * 2nd/6th/9th partials for a glassy, slightly inharmonic character.
 */
static int16_t make_dc_shimmer(int i) {
    int32_t f1 = sin_fx(i);
    int32_t f2 = sin_fx(i * 2);
    int32_t f6 = sin_fx(i * 6);
    int32_t f9 = sin_fx(i * 9);

    int32_t val = (f1 * 52) + (f2 * 30) + (f6 * 14) + (f9 * 8);
    return (int16_t)clamp16(val);
}

static uint32_t s_seq_frame = 0;
static int s_sound_initialized = 0;

/*
 * Wavetable IDs:
 *   0 = chime (lead)
 *   1 = sub drone
 *   2 = body swell (pad)
 *   3 = shimmer (secondary sparkle)
 */
#define WAV_CHIME  0
#define WAV_SUB    1
#define WAV_PAD    2
#define WAV_SHIM   3

/*
 * Echo/reverb channel bank.
 *
 * Channels 0..14 are the direct hits (unchanged layout from before).
 * Channels 16..28 are reserved purely for pseudo-reverb repeats, so
 * an echo can never steal a channel a direct voice is still using.
 */
#define ECHO_CH_BASE 16

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
          - 0:    Chime (bright bell lead)
          - 512:  Sub drone
          - 1024: Body swell (pad)
          - 1536: Shimmer (secondary sparkle) */
    volatile int16_t *wav_chime = (volatile int16_t *)(AICA_RAM_BASE + 0);
    volatile int16_t *wav_sub   = (volatile int16_t *)(AICA_RAM_BASE + 512);
    volatile int16_t *wav_body  = (volatile int16_t *)(AICA_RAM_BASE + 1024);
    volatile int16_t *wav_shim  = (volatile int16_t *)(AICA_RAM_BASE + 1536);

    for (int i = 0; i < 256; i++) {
        wav_chime[i] = make_dc_chime(i);
        wav_sub[i]   = make_dc_sub_drone(i);
        wav_body[i]  = make_dc_body_swell(i);
        wav_shim[i]  = make_dc_shimmer(i);
    }

    s_seq_frame = 0;
    s_sound_initialized = 1;
}

void sound_play_note(int ch, int midi_note, int volume, int pan, int wavetable_id) {
    if (ch < 0 || ch >= 64 || !s_sound_initialized) return;

    if (midi_note < 24)  midi_note = 24;
    if (midi_note > 107) midi_note = 107;

    uint32_t pitch = MIDI_PITCH_TABLE[midi_note - 24];

    uint32_t smp_offset;
    switch (wavetable_id) {
        case WAV_SUB:  smp_offset = 512;  break;
        case WAV_PAD:  smp_offset = 1024; break;
        case WAV_SHIM: smp_offset = 1536; break;
        default:       smp_offset = 0;    break;
    }

    /* Stop previous voice */
    AICA_CHN_REG(ch, 0x00) = 0x8000;

    /* Start address & 256-sample loop */
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;

    /* Pitch */
    AICA_CHN_REG(ch, 0x18) = pitch;

    /* No LFO by default - see sound_play_note_lfo() for the variant
       that turns this on. Writing 0 here guarantees a clean retrigger
       even if a previous note left the LFO running on this channel. */
    AICA_CHN_REG(ch, 0x1C) = 0x0000;

    /* ADSR envelopes tailored per instrument */
    if (wavetable_id == WAV_CHIME) {
        /* Chime: instantaneous acoustic attack, long natural bell decay */
        AICA_CHN_REG(ch, 0x10) = 0x5FC0;
        AICA_CHN_REG(ch, 0x14) = 0x3CA8;
    } else if (wavetable_id == WAV_SUB) {
        /* Deep sub: punchy transient, sustained sub-bass tail */
        AICA_CHN_REG(ch, 0x10) = 0x1F00;
        AICA_CHN_REG(ch, 0x14) = 0x3C06;
    } else if (wavetable_id == WAV_PAD) {
        /* Ethereal pad: gentle swell, long warm release */
        AICA_CHN_REG(ch, 0x10) = 0x0108;
        AICA_CHN_REG(ch, 0x14) = 0x3C86;
    } else {
        /* Shimmer: quick, quiet, decays faster than the lead chime so
           it never outstays the voice it's echoing */
        AICA_CHN_REG(ch, 0x10) = 0x5FC0;
        AICA_CHN_REG(ch, 0x14) = 0x3CE8;
    }

    /* AICA Direct Send Level (0 = loudest/0dB, 15 = muted) */
    uint32_t disdl = (volume >= 15) ? 0 : (15 - (volume & 0x0F));
    AICA_CHN_REG(ch, 0x24) = (disdl << 8) | (pan & 0x1F);

    /* Bypass LPF */
    AICA_CHN_REG(ch, 0x28) = 0x0024;

    /* Key On: 16-bit PCM, Loop Enabled */
    AICA_CHN_REG(ch, 0x00) = 0xC200 | (smp_offset >> 16);
}

/*
 * Same as sound_play_note(), but also arms the AICA's onboard
 * per-channel LFO (register 0x1C) instead of leaving the voice
 * static.
 *
 * lfo_mode:
 *   0 = pitch vibrato   (subtle, for a "live" bell/chime tone)
 *   1 = amplitude throb (slow pulse, good on sustained bass/pads)
 *
 * depth: 0..7, sensitivity of the modulation - keep this low (1-3)
 * for anything meant to sound subtle rather than wobbly.
 *
 * NOTE ON HARDWARE ASSUMPTIONS: this uses the standard, publicly
 * documented AICA LFO register layout (LFORE:1, LFOF:5, ALFOS:3,
 * ALFOWS:2, PLFOS:3, PLFOWS:2, packed MSB-to-LSB in that order).
 * If your sound.h defines named bitfields for this register, prefer
 * those - the raw values below are a safe, conservative starting
 * point, not a guarantee of exact scaling on your build.
 */
static void sound_play_note_lfo(
    int ch, int midi_note, int volume, int pan,
    int wavetable_id, int lfo_mode, int depth)
{
    sound_play_note(ch, midi_note, volume, pan, wavetable_id);

    depth = (int)clampi(depth, 0, 7);

    uint32_t freq_idx   = 3;   /* slow LFO rate   */
    uint32_t waveform   = 2;   /* triangle - smoothest option */
    uint32_t lfo_value;

    if (lfo_mode == 0) {
        /* pitch vibrato: PLFOS/PLFOWS active, ALFOS/ALFOWS silent */
        lfo_value = (freq_idx << 10) | ((uint32_t)depth << 2) | waveform;
    } else {
        /* amplitude throb: ALFOS/ALFOWS active, PLFOS/PLFOWS silent */
        lfo_value = (freq_idx << 10) | ((uint32_t)depth << 7) | (waveform << 5);
    }

    /* Reset the LFO phase, then start it - some AICA implementations
       need the reset pulse to land in its own write to take effect. */
    AICA_CHN_REG(ch, 0x1C) = 0x8000;
    AICA_CHN_REG(ch, 0x1C) = (uint16_t)lfo_value;
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
 * Boot jingle sequence - original tribute composition
 * Exactly 8.0 seconds (480 frames @ 60 FPS), one-shot
 *
 * Pseudo-reverb pattern: every important direct hit (channels 0..11)
 * gets 1-2 quiet repeats a few frames later on a channel in the
 * ECHO_CH_BASE bank, at reduced volume and a nudged pan, using the
 * shimmer instrument instead of the raw chime so the tail reads as
 * "room ambience" rather than an obvious delay repeat. The original
 * long decaying tail (now on shimmer) still finishes the sequence.
 * ------------------------------------------------------------------------- */
void sound_tick(void) {
    if (!s_sound_initialized) return;

    uint32_t tick = s_seq_frame;

    switch (tick) {

        /* ---- Rising spiral, direct hits + early reflections ---- */

        case 0:
            sound_play_note(0, 84, 13, 0x1A, WAV_CHIME); /* C6 (Left)  */
            break;

        case 4:
            /* early reflection of C6 */
            sound_play_note(ECHO_CH_BASE + 0, 84, 6, 0x0E, WAV_SHIM);
            break;

        case 5:
            sound_play_note(1, 86, 13, 0x06, WAV_CHIME); /* D6 (Right) */
            break;

        case 9:
            sound_play_note(ECHO_CH_BASE + 0, 84, 3, 0x1E, WAV_SHIM); /* C6 late tap */
            sound_play_note(ECHO_CH_BASE + 1, 86, 6, 0x12, WAV_SHIM); /* D6 early tap */
            break;

        case 10:
            sound_play_note(2, 91, 14, 0x18, WAV_CHIME); /* G6 (Left)  */
            break;

        case 14:
            sound_play_note(ECHO_CH_BASE + 1, 86, 3, 0x00, WAV_SHIM); /* D6 late tap */
            sound_play_note(ECHO_CH_BASE + 2, 91, 6, 0x0C, WAV_SHIM); /* G6 early tap */
            break;

        case 15:
            sound_play_note(3, 93, 14, 0x08, WAV_CHIME); /* A6 (Right) */
            break;

        case 19:
            sound_play_note(ECHO_CH_BASE + 2, 91, 3, 0x1C, WAV_SHIM); /* G6 late tap */
            sound_play_note(ECHO_CH_BASE + 3, 93, 6, 0x04, WAV_SHIM); /* A6 early tap */
            break;

        case 20:
            sound_play_note(4, 96, 15, 0x16, WAV_CHIME); /* C7 (Left)  */
            break;

        case 24:
            sound_play_note(ECHO_CH_BASE + 3, 93, 3, 0x10, WAV_SHIM); /* A6 late tap */
            sound_play_note(ECHO_CH_BASE + 4, 96, 7, 0x0A, WAV_SHIM); /* C7 early tap */
            break;

        case 26:
            sound_play_note(5, 98, 15, 0x0A, WAV_CHIME); /* D7 (Right) */
            break;

        case 29:
            sound_play_note(ECHO_CH_BASE + 4, 96, 3, 0x1A, WAV_SHIM); /* C7 late tap */
            break;

        case 30:
            sound_play_note(ECHO_CH_BASE + 5, 98, 7, 0x02, WAV_SHIM); /* D7 early tap */
            break;

        case 33:
            /* High peak chime, with a touch of pitch vibrato so it
               feels alive rather than a flat static tone */
            sound_play_note_lfo(6, 103, 15, 0x00, WAV_CHIME, /*pitch*/ 0, /*depth*/ 2);
            break;

        case 35:
            sound_play_note(ECHO_CH_BASE + 5, 98, 3, 0x18, WAV_SHIM); /* D7 late tap */
            break;

        case 37:
            sound_play_note(ECHO_CH_BASE + 6, 103, 6, 0x08, WAV_SHIM); /* G7 early tap */
            break;

        /* ---- Impact: sub-bass, pad, and accent, each with a throb/tail ---- */

        case 38:
            /* Deep sub-bass thump, with a slow amplitude throb */
            sound_play_note_lfo(7, 36, 15, 0x14, WAV_SUB, /*amp*/ 1, /*depth*/ 2);
            sound_play_note(8, 36, 15, 0x0C, WAV_SUB);

            /* Warm harmonizing low-mid body */
            sound_play_note(9, 48, 13, 0x12, WAV_PAD);
            sound_play_note(10, 55, 12, 0x0E, WAV_PAD);

            /* Sparkling accent drop */
            sound_play_note(11, 84, 14, 0x00, WAV_CHIME);
            break;

        case 42:
            sound_play_note(ECHO_CH_BASE + 6, 103, 3, 0x10, WAV_SHIM); /* G7 late tap  */
            sound_play_note(ECHO_CH_BASE + 7, 84, 6, 0x08, WAV_SHIM);  /* accent early tap */
            break;

        case 46:
            sound_play_note(ECHO_CH_BASE + 7, 84, 3, 0x18, WAV_SHIM); /* accent late tap */
            break;

        /* ---- Long decaying tail as the room settles ---- */

        case 48:
            sound_play_note(12, 96, 12, 0x19, WAV_SHIM); /* C7 (decaying tail) */
            break;
        case 58:
            sound_play_note(13, 91, 11, 0x07, WAV_SHIM); /* G6 (decaying tail) */
            break;
        case 70:
            sound_play_note(14, 84, 10, 0x00, WAV_SHIM); /* C6 (decaying tail) */
            break;
        case 85:
            sound_play_note(ECHO_CH_BASE + 8, 84, 4, 0x14, WAV_SHIM); /* last, faint breath */
            break;

        /* ---- End of 8.0-second sequence ---- */
        case 479:
            sound_stop();
            return;

        default:
            break;
    }

    s_seq_frame++;
}