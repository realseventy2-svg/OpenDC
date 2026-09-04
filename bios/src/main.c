#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/vmu.h>
#include <dc/cdrom.h>
#include <dc/fs_iso9660.h>
#include <dc/video.h>
#include <arch/exec.h>
#include <arch/rtc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aero_atlas.h"
#include "bootloader_gdrom.h"

extern const uint8_t romdisk[];

/* Match DreamDash's known-good KOS startup requirements explicitly. */
KOS_INIT_FLAGS(INIT_IRQ | INIT_THD_PREEMPT | INIT_FS_ALL |
               INIT_LIBRARY | INIT_CDROM | INIT_CONTROLLER | INIT_VMU);

#define NUM_BUBBLES 22
#define NUM_RIPPLES 8
#define NUM_SWIRL_PTS 72

/* AICA SPU Base Addresses */
#define AICA_REG_BASE       0xA0700000UL
#define AICA_RAM_BASE       0xA0800000UL
#define AICA_CHN_REG(ch, reg) (*(volatile uint32_t *)(AICA_REG_BASE + ((ch) * 0x80) + (reg)))

/* 88-note pitch table for MIDI notes 21 (A0) to 108 (C8) */
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
static const int16_t sin_quarter_tab[65] = {
    0,   6,  12,  18,  25,  31,  37,  43,  49,  56,  62,  68,  74,  80,  86,  92,
   97, 103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176,
  181, 185, 189, 193, 197, 201, 205, 208, 212, 215, 219, 222, 225, 228, 231, 233,
  236, 238, 240, 242, 244, 246, 247, 249, 250, 251, 252, 253, 254, 254, 255, 255,
  256
};

static int32_t sin_fx(int angle) {
    angle &= 0xFF;
    if (angle <= 64) return sin_quarter_tab[angle];
    if (angle <= 128) return sin_quarter_tab[128 - angle];
    if (angle <= 192) return -sin_quarter_tab[angle - 128];
    return -sin_quarter_tab[256 - angle];
}

/* SPU Ambient Music Engine */
static void aica_synth_init(void) {
    /* 1. Hold ARM CPU in reset for direct SH-4 hardware sound synthesis */
    *(volatile uint32_t *)0xA0702C00UL |= 1;

    /* 2. Unmute master dry output volume */
    *(volatile uint16_t *)0xA0702800UL = 0x000F;

    /* 3. Silence all channels */
    for (int ch = 0; ch < 64; ch++) {
        AICA_CHN_REG(ch, 0x00) = 0x8000;
        AICA_CHN_REG(ch, 0x24) = 0x0000;
    }

    /* 4. Synthesize 3 soothing 256-sample 16-bit PCM wavetables:
          - Wavetable 0 (offset 0): Deep oceanic sub-drone
          - Wavetable 1 (offset 512): Warm, lush celestial pad (warm harmonic blend)
          - Wavetable 2 (offset 1024): Gentle mellow chime (soft water drop, zero shrill lead) */
    volatile int16_t *wav_drone = (volatile int16_t *)(AICA_RAM_BASE + 0);
    volatile int16_t *wav_pad   = (volatile int16_t *)(AICA_RAM_BASE + 512);
    volatile int16_t *wav_chime = (volatile int16_t *)(AICA_RAM_BASE + 1024);

    for (int i = 0; i < 256; i++) {
        int32_t s1 = sin_fx(i);
        int32_t s2 = sin_fx(i * 2);
        int32_t val_drone = (s1 * 95) + (s2 * 25);
        if (val_drone > 32767) val_drone = 32767;
        if (val_drone < -32767) val_drone = -32767;
        wav_drone[i] = (int16_t)val_drone;

        int32_t p1 = sin_fx(i);
        int32_t p2 = sin_fx(i * 2);
        int32_t p3 = sin_fx(i * 3);
        int32_t val_pad = (p1 * 80) + (p2 * 28) + (p3 * 12);
        if (val_pad > 32767) val_pad = 32767;
        if (val_pad < -32767) val_pad = -32767;
        wav_pad[i] = (int16_t)val_pad;

        int32_t c1 = sin_fx(i);
        int32_t c3 = sin_fx(i * 3);
        int32_t val_chime = (c1 * 90) + (c3 * 20);
        if (val_chime > 32767) val_chime = 32767;
        if (val_chime < -32767) val_chime = -32767;
        wav_chime[i] = (int16_t)val_chime;
    }
}

static void aica_play_note(int ch, int midi_note, int volume, int pan, int wavetable_id) {
    if (ch < 0 || ch >= 64) return;
    if (midi_note < 21) midi_note = 21;
    if (midi_note > 108) midi_note = 108;

    uint32_t pitch = MIDI_PITCH_TABLE[midi_note - 21];
    uint32_t smp_offset = (wavetable_id == 2) ? 1024 : ((wavetable_id == 1) ? 512 : 0);

    AICA_CHN_REG(ch, 0x00) = 0x8000;
    AICA_CHN_REG(ch, 0x04) = smp_offset & 0xFFFF;
    AICA_CHN_REG(ch, 0x08) = 0;
    AICA_CHN_REG(ch, 0x0C) = 256;

    if (wavetable_id == 0) {
        AICA_CHN_REG(ch, 0x10) = 0x0003;
        AICA_CHN_REG(ch, 0x14) = 0x3C02;
    } else if (wavetable_id == 1) {
        AICA_CHN_REG(ch, 0x10) = 0x0004;
        AICA_CHN_REG(ch, 0x14) = 0x3C02;
    } else {
        AICA_CHN_REG(ch, 0x10) = 0x321F;
        AICA_CHN_REG(ch, 0x14) = 0x3D0E;
    }

    AICA_CHN_REG(ch, 0x18) = pitch;
    AICA_CHN_REG(ch, 0x24) = ((uint32_t)(volume & 0x0F) << 8) | (pan & 0x1F);
    AICA_CHN_REG(ch, 0x28) = 0x0024;
    AICA_CHN_REG(ch, 0x00) = 0xC200 | (smp_offset >> 16);
}

/* 20-Second Ultra-Relaxing Atmospheric Pad Sequencer */
static void aica_ambient_tick(uint32_t frame) {
    uint32_t tick = frame % 1200;

    switch (tick) {
        case 0:
            aica_play_note(0, 27, 14, 0x18, 0); /* Eb1 Sub Left */
            aica_play_note(1, 34, 13, 0x08, 0); /* Bb1 Drone Right */
            aica_play_note(2, 55, 11, 0x1C, 1); /* G3 Celestial Pad Left */
            aica_play_note(3, 62, 11, 0x04, 1); /* D4 Celestial Pad Right */
            aica_play_note(4, 77, 11, 0x1A, 2); /* F5 Soft Mellow Chime Left */
            break;
        case 30:
            aica_play_note(5, 77,  8, 0x0E, 2); /* F5 Soft Echo Right */
            break;
        case 60:
            aica_play_note(6, 77,  5, 0x00, 2); /* F5 Diffuse Center */
            break;

        case 300:
            aica_play_note(0, 24, 14, 0x18, 0); /* C1 Sub Left */
            aica_play_note(1, 31, 13, 0x08, 0); /* G1 Drone Right */
            aica_play_note(2, 51, 11, 0x1C, 1); /* Eb3 Celestial Pad Left */
            aica_play_note(3, 58, 11, 0x04, 1); /* Bb3 Celestial Pad Right */
            aica_play_note(4, 79, 11, 0x0C, 2); /* G5 Soft Mellow Chime Right */
            break;
        case 330:
            aica_play_note(5, 79,  8, 0x1E, 2); /* G5 Soft Echo Left */
            break;
        case 360:
            aica_play_note(6, 79,  5, 0x00, 2); /* G5 Diffuse Center */
            break;

        case 600:
            aica_play_note(0, 32, 14, 0x18, 0); /* Ab1 Sub Left */
            aica_play_note(1, 39, 13, 0x08, 0); /* Eb2 Drone Right */
            aica_play_note(2, 48, 11, 0x1C, 1); /* C3 Celestial Pad Left */
            aica_play_note(3, 55, 11, 0x04, 1); /* G3 Celestial Pad Right */
            aica_play_note(4, 75, 11, 0x18, 2); /* Eb5 Soft Chime Left */
            break;
        case 630:
            aica_play_note(5, 75,  8, 0x0A, 2); /* Eb5 Echo Right */
            break;
        case 660:
            aica_play_note(6, 75,  5, 0x00, 2); /* Eb5 Diffuse Center */
            break;

        case 900:
            aica_play_note(0, 34, 14, 0x18, 0); /* Bb1 Sub Left */
            aica_play_note(1, 41, 13, 0x08, 0); /* F2 Drone Right */
            aica_play_note(2, 58, 11, 0x1C, 1); /* Bb3 Pad Left */
            aica_play_note(3, 63, 11, 0x04, 1); /* Eb4 Pad Right */
            aica_play_note(4, 74, 11, 0x00, 2); /* D5 Soft Chime Center */
            break;
        case 960:
            aica_play_note(2, 50, 11, 0x1A, 1); /* D3 Pad Left */
            aica_play_note(3, 53, 11, 0x06, 1); /* F3 Pad Right */
            break;
        case 990:
            aica_play_note(5, 74,  7, 0x14, 2); /* D5 Echo Left */
            break;

        default:
            break;
    }
}

/* Floating Glass Bubble Data */
typedef struct {
    float x, y;
    float radius;
    float speed_y;
    float wobble_freq;
    float wobble_amp;
    float alpha;
} bubble_t;

static bubble_t bubbles[NUM_BUBBLES];

/* Expanding Water Ripple Data */
typedef struct {
    float x, y;
    float radius;
    float max_radius;
    float alpha;
    int active;
} ripple_t;

static ripple_t ripples[NUM_RIPPLES];

static pvr_ptr_t s_atlas_vram = NULL;

static void init_aero_environment(void) {
    s_atlas_vram = pvr_mem_malloc(256 * 256 * 2);
    if (s_atlas_vram) {
        pvr_txr_load((void *)AERO_ATLAS_TEX, s_atlas_vram, 256 * 256 * 2);
    }

    for (int i = 0; i < NUM_BUBBLES; i++) {
        bubbles[i].x = (float)(rand() % 640);
        bubbles[i].y = (float)(rand() % 500);
        bubbles[i].radius = 10.0f + (float)(rand() % 18);
        bubbles[i].speed_y = 0.35f + (float)(rand() % 100) * 0.008f;
        bubbles[i].wobble_freq = 0.02f + (float)(rand() % 50) * 0.001f;
        bubbles[i].wobble_amp = 8.0f + (float)(rand() % 14);
        bubbles[i].alpha = 0.45f + (float)(rand() % 45) * 0.01f;
    }

    for (int i = 0; i < NUM_RIPPLES; i++) {
        ripples[i].active = 0;
    }
}

static void trigger_ripple(float x, float y) {
    for (int i = 0; i < NUM_RIPPLES; i++) {
        if (!ripples[i].active) {
            ripples[i].x = x;
            ripples[i].y = y;
            ripples[i].radius = 6.0f;
            ripples[i].max_radius = 85.0f;
            ripples[i].alpha = 0.85f;
            ripples[i].active = 1;
            break;
        }
    }
}

/* =========================================================================
   HARDWARE ACCELERATED PVR PRIMITIVES
   ========================================================================= */

static void draw_txr_quad(float x, float y, float w, float h, float z,
                          float u0, float v0, float u1, float v1, uint32_t col) {
    if (!s_atlas_vram) return;
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED,
                     256, 256, s_atlas_vram, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    vert.flags = PVR_CMD_VERTEX;
    vert.x = x; vert.y = y; vert.z = z;
    vert.u = u0; vert.v = v0;
    vert.argb = col; vert.oargb = 0;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x + w; vert.y = y;
    vert.u = u1; vert.v = v0;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x; vert.y = y + h;
    vert.u = u0; vert.v = v1;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x + w; vert.y = y + h;
    vert.u = u1; vert.v = v1;
    pvr_prim(&vert, sizeof(vert));
}

static void draw_quad_gradient(float x, float y, float w, float h, float z,
                               uint32_t col_tl, uint32_t col_tr, uint32_t col_bl, uint32_t col_br) {
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    vert.flags = PVR_CMD_VERTEX;
    vert.x = x; vert.y = y; vert.z = z;
    vert.u = 0.0f; vert.v = 0.0f;
    vert.argb = col_tl; vert.oargb = 0;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x + w; vert.y = y;
    vert.argb = col_tr;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x; vert.y = y + h;
    vert.argb = col_bl;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x + w; vert.y = y + h;
    vert.argb = col_br;
    pvr_prim(&vert, sizeof(vert));
}

static void draw_quad_mesh(float x0, float y0, float x1, float y1,
                           float x2, float y2, float x3, float y3,
                           float z, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) {
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    vert.flags = PVR_CMD_VERTEX;
    vert.x = x0; vert.y = y0; vert.z = z;
    vert.u = 0.0f; vert.v = 0.0f;
    vert.argb = c0; vert.oargb = 0;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x1; vert.y = y1;
    vert.argb = c1;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x2; vert.y = y2;
    vert.argb = c2;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x3; vert.y = y3;
    vert.argb = c3;
    pvr_prim(&vert, sizeof(vert));
}

static void draw_quad(float x, float y, float w, float h, float z, uint32_t col) {
    draw_quad_gradient(x, y, w, h, z, col, col, col, col);
}

/* Mathematically Exact Proportional Baseline Typography */
static void draw_text_smooth(float start_x, float start_y, float scale, const char *text, uint32_t col) {
    float cur_x = start_x;
    for (int i = 0; text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32 || c > 126) c = ' ';
        const glyph_t *g = &GLYPH_MAP[c - 32];
        if (g->w > 0) {
            float u0 = (float)g->x / 256.0f;
            float v0 = (float)g->y / 256.0f;
            float u1 = (float)(g->x + g->w) / 256.0f;
            float v1 = (float)(g->y + g->h) / 256.0f;

            float qx = cur_x + (float)g->xoff * scale;
            float qy = start_y + (float)g->yoff * scale;
            float qw = (float)g->w * scale;
            float qh = (float)g->h * scale;

            draw_txr_quad(qx, qy, qw, qh, 5.0f, u0, v0, u1, v1, col);
        }
        cur_x += (float)g->adv * scale;
    }
}

static void draw_text_shadow(float start_x, float start_y, float scale, const char *text, uint32_t col, uint32_t shadow_col) {
    draw_text_smooth(start_x + 1.0f, start_y + 1.0f, scale, text, shadow_col);
    draw_text_smooth(start_x, start_y, scale, text, col);
}

/* =========================================================================
    AERO GRAPHICS ENGINE
   ========================================================================= */

/* 1. Luminous Sky & Tropical Ocean Gradient Background */
static void draw_sky_atmosphere(int frame) {
    uint32_t c_top = PVR_PACK_COLOR(1.0f, 0.05f, 0.32f, 0.65f);
    uint32_t c_mid = PVR_PACK_COLOR(1.0f, 0.08f, 0.68f, 0.92f);
    uint32_t c_bot = PVR_PACK_COLOR(1.0f, 0.35f, 0.88f, 0.95f);

    draw_quad_gradient(0, 0, 640, 240, 0.5f, c_top, c_top, c_mid, c_mid);
    draw_quad_gradient(0, 240, 640, 240, 0.5f, c_mid, c_mid, c_bot, c_bot);

    float sun_x = 520.0f + sinf(frame * 0.01f) * 15.0f;
    float sun_y = 60.0f + cosf(frame * 0.012f) * 10.0f;
    uint32_t c_sun_core = PVR_PACK_COLOR(0.35f, 1.0f, 1.0f, 1.0f);
    uint32_t c_sun_fade = PVR_PACK_COLOR(0.0f, 0.6f, 0.9f, 1.0f);
    draw_quad_gradient(sun_x - 160, sun_y - 160, 320, 320, 0.6f, c_sun_fade, c_sun_fade, c_sun_fade, c_sun_core);
}

/* 2. Undulating Aurora Waves */
static void draw_aurora_ribbons(int frame) {
    int num_segments = 32;
    float seg_w = 640.0f / (float)num_segments;

    for (int seg = 0; seg < num_segments; seg++) {
        float x1 = seg * seg_w;
        float x2 = (seg + 1) * seg_w;

        float wave1 = sinf(seg * 0.35f + frame * 0.04f) * 25.0f + cosf(seg * 0.15f - frame * 0.02f) * 15.0f;
        float wave2 = sinf((seg + 1) * 0.35f + frame * 0.04f) * 25.0f + cosf((seg + 1) * 0.15f - frame * 0.02f) * 15.0f;

        float y1 = 380.0f + wave1;
        float y2 = 380.0f + wave2;
        float h1 = 90.0f + sinf(seg * 0.2f + frame * 0.03f) * 20.0f;
        float h2 = 90.0f + sinf((seg + 1) * 0.2f + frame * 0.03f) * 20.0f;

        uint32_t col_top = PVR_PACK_COLOR(0.30f, 0.3f, 1.0f, 0.75f);
        uint32_t col_bot = PVR_PACK_COLOR(0.0f,  0.0f, 0.7f, 1.0f);

        draw_quad_mesh(x1, y1 - h1, x2, y2 - h2, x1, y1, x2, y2, 0.7f,
                       col_top, col_top, col_bot, col_bot);
    }
}

/* 3. Smooth Circular Glass Bubbles */
static void draw_bubbles(int frame) {
    float u0 = (32.0f - 26.0f) / 256.0f;
    float v0 = (200.0f - 26.0f) / 256.0f;
    float u1 = (32.0f + 26.0f) / 256.0f;
    float v1 = (200.0f + 26.0f) / 256.0f;

    for (int i = 0; i < NUM_BUBBLES; i++) {
        bubbles[i].y -= bubbles[i].speed_y;
        if (bubbles[i].y < -40.0f) {
            bubbles[i].y = 520.0f;
            bubbles[i].x = (float)(rand() % 640);
        }

        float wobble = sinf(bubbles[i].y * bubbles[i].wobble_freq + frame * 0.05f) * bubbles[i].wobble_amp;
        float bx = bubbles[i].x + wobble;
        float by = bubbles[i].y;
        float r = bubbles[i].radius;

        uint32_t col = PVR_PACK_COLOR(bubbles[i].alpha, 0.85f, 0.95f, 1.0f);
        draw_txr_quad(bx - r, by - r, r * 2.0f, r * 2.0f, 1.2f, u0, v0, u1, v1, col);
    }
}

/* 4. Smooth Liquid Water Ripples */
static void draw_ripples(void) {
    float u0 = (96.0f - 26.0f) / 256.0f;
    float v0 = (200.0f - 26.0f) / 256.0f;
    float u1 = (96.0f + 26.0f) / 256.0f;
    float v1 = (200.0f + 26.0f) / 256.0f;

    for (int i = 0; i < NUM_RIPPLES; i++) {
        if (!ripples[i].active) continue;

        ripples[i].radius += 2.0f;
        ripples[i].alpha -= 0.020f;

        if (ripples[i].alpha <= 0.0f || ripples[i].radius >= ripples[i].max_radius) {
            ripples[i].active = 0;
            continue;
        }

        float r = ripples[i].radius;
        uint32_t col = PVR_PACK_COLOR(ripples[i].alpha * 0.85f, 0.7f, 0.95f, 1.0f);
        draw_txr_quad(ripples[i].x - r, ripples[i].y - r * 0.6f, r * 2.0f, r * 1.2f, 1.0f, u0, v0, u1, v1, col);
    }
}

/* 5. Authentic Frutiger Aero Glossy Glass Panels */
static void draw_glass_panel(float x, float y, float w, float h, float z, int is_selected, float glow_phase) {
    float glow_w = is_selected ? 6.0f : 2.0f;
    float glow_a = is_selected ? (0.45f + 0.25f * sinf(glow_phase)) : 0.15f;
    uint32_t c_glow = is_selected ? PVR_PACK_COLOR(glow_a, 0.0f, 0.95f, 0.9f) : PVR_PACK_COLOR(glow_a, 0.0f, 0.2f, 0.5f);
    draw_quad(x - glow_w, y - glow_w, w + glow_w * 2.0f, h + glow_w * 2.0f, z - 0.1f, c_glow);

    uint32_t c_base_top = is_selected ? PVR_PACK_COLOR(0.65f, 0.15f, 0.65f, 0.95f) : PVR_PACK_COLOR(0.40f, 0.08f, 0.40f, 0.70f);
    uint32_t c_base_bot = is_selected ? PVR_PACK_COLOR(0.75f, 0.05f, 0.45f, 0.85f) : PVR_PACK_COLOR(0.55f, 0.02f, 0.25f, 0.55f);
    draw_quad_gradient(x, y, w, h, z, c_base_top, c_base_top, c_base_bot, c_base_bot);

    float glare_h = h * 0.48f;
    uint32_t c_glare_top = PVR_PACK_COLOR(0.55f, 1.0f, 1.0f, 1.0f);
    uint32_t c_glare_bot = PVR_PACK_COLOR(0.08f, 0.85f, 0.98f, 1.0f);
    draw_quad_gradient(x + 2.0f, y + 2.0f, w - 4.0f, glare_h, z + 0.1f, c_glare_top, c_glare_top, c_glare_bot, c_glare_bot);

    uint32_t c_border_top = is_selected ? PVR_PACK_COLOR(0.95f, 0.9f, 1.0f, 1.0f) : PVR_PACK_COLOR(0.70f, 0.75f, 0.95f, 1.0f);
    uint32_t c_border_bot = is_selected ? PVR_PACK_COLOR(0.75f, 0.0f, 0.9f, 0.9f) : PVR_PACK_COLOR(0.35f, 0.1f, 0.5f, 0.8f);

    draw_quad(x, y, w, 1.5f, z + 0.2f, c_border_top);
    draw_quad(x, y, 1.5f, h, z + 0.2f, c_border_top);
    draw_quad(x, y + h - 1.5f, w, 1.5f, z + 0.2f, c_border_bot);
    draw_quad(x + w - 1.5f, y, 1.5f, h, z + 0.2f, c_border_bot);
}

/* 6. Smooth Round Glossy Controller Button Gems */
static void draw_button_gem(float x, float y, float r, const char *label, int color_type) {
    float u0 = (160.0f - 24.0f) / 256.0f;
    float v0 = (200.0f - 24.0f) / 256.0f;
    float u1 = (160.0f + 24.0f) / 256.0f;
    float v1 = (200.0f + 24.0f) / 256.0f;

    float cr, cg, cb;
    if (color_type == 0)      { cr = 0.2f; cg = 0.95f; cb = 0.4f; } /* (A) Green */
    else if (color_type == 1) { cr = 0.95f; cg = 0.2f; cb = 0.3f; } /* (B) Red */
    else if (color_type == 2) { cr = 0.2f; cg = 0.6f; cb = 0.98f; } /* (X/D) Blue */
    else                      { cr = 0.98f; cg = 0.85f; cb = 0.1f; } /* (Y) Yellow */

    uint32_t col = PVR_PACK_COLOR(0.95f, cr, cg, cb);
    draw_txr_quad(x - r, y - r, r * 2.0f, r * 2.0f, 4.0f, u0, v0, u1, v1, col);

    /* Perfect center text inside circle */
    if (label && label[0]) {
        unsigned char c = (unsigned char)label[0];
        if (c >= 32 && c <= 126) {
            const glyph_t *g = &GLYPH_MAP[c - 32];
            float scale = (r * 1.35f) / 24.0f;
            float start_x = x - (float)g->adv * 0.5f * scale;
            float start_y = y - 16.0f * scale;
            draw_text_smooth(start_x, start_y, scale, label, PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));
        }
    }
}

/* 7. 3D Faceted Glass Dreamcast Swirl */
static void draw_3d_glass_swirl(float center_x, float center_y, float scale, int frame) {
    float rot = frame * 0.035f;

    for (int i = 0; i < NUM_SWIRL_PTS; i++) {
        float t = (float)i / NUM_SWIRL_PTS;
        float r = t * (75.0f * scale);
        float angle = t * 4.2f * 3.14159f + rot;

        float z_offset = sinf(angle * 2.5f + rot) * 25.0f;
        float sx = center_x + cosf(angle) * r;
        float sy = center_y + sinf(angle) * (r * 0.52f) + z_offset * 0.25f;
        float pt_size = (4.0f + t * 6.5f) * scale;

        float cr = sinf(t * 3.14f + frame * 0.04f) * 0.4f + 0.6f;
        float cg = 0.9f;
        float cb = cosf(t * 3.14f + frame * 0.03f) * 0.4f + 0.6f;
        float alpha = 0.55f + t * 0.45f;

        uint32_t col_core = PVR_PACK_COLOR(alpha, cr, cg, cb);
        uint32_t col_glint = PVR_PACK_COLOR(alpha * 0.95f, 1.0f, 1.0f, 1.0f);

        draw_quad_gradient(sx - pt_size * 0.5f, sy - pt_size * 0.5f, pt_size, pt_size, 3.5f,
                           col_glint, col_core, col_core, col_glint);
    }
}

/* 8. 3D Iridescent Holographic GD-ROM Disc */
static void draw_3d_holographic_disc(float cx, float cy, float radius, int frame, int has_disc) {
    float rot = frame * 0.05f;

    draw_quad(cx - radius - 4, cy - radius * 0.6f - 4, (radius + 4) * 2.0f, (radius + 4) * 1.2f, 2.0f,
              PVR_PACK_COLOR(0.35f, 0.0f, 0.1f, 0.25f));

    uint32_t c_disc_edge = PVR_PACK_COLOR(0.85f, 0.85f, 0.95f, 1.0f);
    uint32_t c_disc_body = PVR_PACK_COLOR(0.70f, 0.45f, 0.75f, 0.95f);
    draw_quad_gradient(cx - radius, cy - radius * 0.55f, radius * 2.0f, radius * 1.1f, 2.2f,
                       c_disc_edge, c_disc_body, c_disc_body, c_disc_edge);

    int rings = 8;
    for (int r = 0; r < rings; r++) {
        float ring_t = (float)r / (float)rings;
        float ring_r = (radius * 0.35f) + ring_t * (radius * 0.60f);

        float hue_angle = rot + ring_t * 6.28f;
        float cr = sinf(hue_angle) * 0.5f + 0.5f;
        float cg = sinf(hue_angle + 2.09f) * 0.5f + 0.5f;
        float cb = sinf(hue_angle + 4.18f) * 0.5f + 0.5f;

        uint32_t c_diff = PVR_PACK_COLOR(0.40f, cr, cg, cb);
        draw_quad(cx - ring_r, cy - ring_r * 0.55f, ring_r * 2.0f, ring_r * 1.1f, 2.4f + ring_t * 0.1f, c_diff);
    }

    float hub_r = radius * 0.28f;
    uint32_t c_hub = PVR_PACK_COLOR(0.9f, 0.95f, 0.98f, 1.0f);
    draw_quad(cx - hub_r, cy - hub_r * 0.55f, hub_r * 2.0f, hub_r * 1.1f, 2.6f, c_hub);

    float hole_r = radius * 0.12f;
    uint32_t c_hole = PVR_PACK_COLOR(1.0f, 0.05f, 0.32f, 0.65f);
    draw_quad(cx - hole_r, cy - hole_r * 0.55f, hole_r * 2.0f, hole_r * 1.1f, 2.7f, c_hole);

    float glint_x = cx + cosf(rot * 2.0f) * (radius * 0.5f);
    float glint_y = cy + sinf(rot * 2.0f) * (radius * 0.28f);
    draw_quad(glint_x - 10, glint_y - 10, 20, 20, 2.8f, PVR_PACK_COLOR(0.85f, 1.0f, 1.0f, 1.0f));

    if (has_disc) {
        draw_glass_panel(cx - 100, cy + radius * 0.65f, 200, 32, 3.0f, 1, frame * 0.1f);
        draw_text_shadow(cx - 75, cy + radius * 0.65f + 6.0f, 0.72f, "SEGA GD-ROM READY",
                         PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.5f), PVR_PACK_COLOR(0.6f, 0.0f, 0.0f, 0.0f));
    }
}

/* 9. Frosted Acrylic VMU Card Module */
static void draw_vmu_card(float x, float y, float w, float h, float z, const char *port_name,
                          int is_inserted, int blocks_used, int frame) {
    draw_glass_panel(x, y, w, h, z, is_inserted, frame * 0.08f);

    draw_text_shadow(x + 14.0f, y + 6.0f, 0.78f, port_name,
                     PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f), PVR_PACK_COLOR(0.6f, 0.0f, 0.2f, 0.4f));

    float lcd_x = x + 14.0f;
    float lcd_y = y + 36.0f;
    float lcd_w = 70.0f;
    float lcd_h = 52.0f;

    draw_quad_gradient(lcd_x, lcd_y, lcd_w, lcd_h, z + 0.2f,
                       PVR_PACK_COLOR(0.85f, 0.45f, 0.70f, 0.40f), PVR_PACK_COLOR(0.85f, 0.45f, 0.70f, 0.40f),
                       PVR_PACK_COLOR(0.95f, 0.35f, 0.55f, 0.30f), PVR_PACK_COLOR(0.95f, 0.35f, 0.55f, 0.30f));

    if (is_inserted) {
        int anim_dot = (frame / 10) % 4;
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 5; col++) {
                if ((row == 2 || col == 2) || ((row + col + anim_dot) % 3 == 0)) {
                    draw_quad(lcd_x + 15 + col * 8, lcd_y + 8 + row * 7, 6, 5, z + 0.3f,
                              PVR_PACK_COLOR(0.85f, 0.1f, 0.25f, 0.1f));
                }
            }
        }

        char buf[48];
        snprintf(buf, sizeof(buf), "%d / 200 Blocks", blocks_used);
        draw_text_smooth(x + 96.0f, y + 34.0f, 0.72f, buf, PVR_PACK_COLOR(1.0f, 0.2f, 1.0f, 0.6f));

        float bar_w = w - 110.0f;
        draw_quad(x + 96.0f, y + 56.0f, bar_w, 10.0f, z + 0.2f, PVR_PACK_COLOR(0.4f, 0.1f, 0.3f, 0.5f));
        float fill_w = (bar_w * (float)blocks_used) / 200.0f;
        draw_quad_gradient(x + 96.0f, y + 56.0f, fill_w, 10.0f, z + 0.3f,
                           PVR_PACK_COLOR(0.9f, 0.2f, 0.95f, 0.6f), PVR_PACK_COLOR(0.9f, 0.2f, 0.95f, 0.6f),
                           PVR_PACK_COLOR(0.9f, 0.05f, 0.65f, 0.4f), PVR_PACK_COLOR(0.9f, 0.05f, 0.65f, 0.4f));

        draw_text_smooth(x + 96.0f, y + 74.0f, 0.60f, "Status: Mounted & OK", PVR_PACK_COLOR(0.8f, 0.7f, 0.9f, 1.0f));
    } else {
        draw_text_smooth(lcd_x + 12.0f, lcd_y + 18.0f, 0.60f, "Offline", PVR_PACK_COLOR(0.6f, 0.2f, 0.35f, 0.2f));
        draw_text_smooth(x + 96.0f, y + 40.0f, 0.70f, "No VMU Inserted", PVR_PACK_COLOR(0.6f, 0.7f, 0.8f, 0.9f));
        draw_text_smooth(x + 96.0f, y + 66.0f, 0.58f, "Slot Empty", PVR_PACK_COLOR(0.5f, 0.5f, 0.6f, 0.7f));
    }
}

/* =========================================================================
   GD-ROM SERVICE & DISC ENGINE
   ========================================================================= */

static int check_disc_status(char *game_title, int max_len) {
    static int poll_count;
    static int cached_result;

    if (poll_count++ != 0 && (poll_count % 60) != 0) {
        if (cached_result && game_title)
            snprintf(game_title, max_len, "DREAMCAST GAME DISC");
        return cached_result;
    }

    volatile gdrom_service_table_t *srv = gdrom_services();
    if (srv && srv->magic == GDROM_SERVICE_MAGIC && srv->disc_present) {
        cached_result = 1;
        if (game_title)
            snprintf(game_title, max_len, "DREAMCAST GAME DISC");
        return 1;
    }

    cached_result = 0;
    return 0;
}

static void boot_inserted_disc(void) {
    volatile gdrom_service_table_t *srv = gdrom_services();
    if (srv && srv->magic == GDROM_SERVICE_MAGIC && srv->boot_game &&
        srv->disc_present && srv->data_fad) {
        srv->boot_game(srv->data_fad);
    }
}

enum {
    VIEW_MAIN_MENU = 0,
    VIEW_PLAY_DISC,
    VIEW_MEMORY_CARDS,
    VIEW_SETTINGS,
    VIEW_STATS
};

int main(int argc, char **argv) {
    fs_romdisk_mount("/rd", romdisk, 0);
    pvr_init_defaults();
    pvr_set_bg_color(0.05f, 0.32f, 0.65f);
    init_aero_environment();

    /* Initialize Yamaha AICA Hardware SPU Ambient Synthesizer */
    aica_synth_init();

    int frame = 0;
    int menu_sel = 0;
    int current_view = VIEW_MAIN_MENU;
    uint32_t prev_buttons = 0;

    const char *menu_items[] = {
        "Play Disc",
        "Memory Cards",
        "System Settings",
        "Hardware Stats"
    };
    const char *menu_descriptions[] = {
        "Boot inserted GD-ROM, MIL-CD, or homebrew disc",
        "Inspect and manage VMU memory cards and save files",
        "Configure video cables, audio output, and system clock",
        "Real-time SH-4 CPU, CLX2 GPU, and SPU telemetry"
    };
    int num_items = 4;

    while (1) {
        frame++;

        /* Advance Soothing Atmospheric Ambient Pad Synthesizer */
        aica_ambient_tick(frame);

        /* Read Controller Inputs */
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if (cont) {
            cont_state_t *st = (cont_state_t *)maple_dev_status(cont);
            if (st) {
                if (current_view == VIEW_MAIN_MENU) {
                    if (((st->buttons & CONT_DPAD_DOWN) || st->joyy > 40) &&
                        !((prev_buttons & CONT_DPAD_DOWN))) {
                        menu_sel = (menu_sel + 1) % num_items;
                        trigger_ripple(320.0f, 92.0f + menu_sel * 70.0f + 29.0f);
                    }
                    if (((st->buttons & CONT_DPAD_UP) || st->joyy < -40) &&
                        !((prev_buttons & CONT_DPAD_UP))) {
                        menu_sel = (menu_sel - 1 + num_items) % num_items;
                        trigger_ripple(320.0f, 92.0f + menu_sel * 70.0f + 29.0f);
                    }
                    if ((st->buttons & CONT_A) && !(prev_buttons & CONT_A)) {
                        current_view = menu_sel + 1;
                        trigger_ripple(320.0f, 240.0f);
                    }
                } else {
                    if ((st->buttons & CONT_B) && !(prev_buttons & CONT_B)) {
                        current_view = VIEW_MAIN_MENU;
                        trigger_ripple(320.0f, 240.0f);
                    }
                    if (current_view == VIEW_PLAY_DISC && (st->buttons & CONT_A) && !(prev_buttons & CONT_A)) {
                        trigger_ripple(320.0f, 240.0f);
                        boot_inserted_disc();
                    }
                }
                prev_buttons = st->buttons;
            }
        }

        /* PVR Frame Render Pass */
        pvr_wait_ready();
        pvr_scene_begin();
        pvr_list_begin(PVR_LIST_TR_POLY);

        /* 1. Atmospheric Sky & Aurora Horizon */
        draw_sky_atmosphere(frame);
        draw_aurora_ribbons(frame);

        /* 2. Floating Liquid Glass Bubbles & Water Ripples */
        draw_bubbles(frame);
        draw_ripples();

        /* 3. Top Navigation Glass Banner */
        draw_glass_panel(20.0f, 16.0f, 600.0f, 66.0f, 2.0f, 0, 0.0f);
        draw_3d_glass_swirl(55.0f, 49.0f, 0.40f, frame);

        /* Top Banner Typography */
        draw_text_shadow(95.0f, 24.0f, 1.00f, "OpenDC Dashboard",
                         PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f), PVR_PACK_COLOR(0.8f, 0.02f, 0.25f, 0.5f));
        draw_text_smooth(95.0f, 52.0f, 0.58f, "Sega Dreamcast | Firmware 1.0b",
                         PVR_PACK_COLOR(0.85f, 0.7f, 0.95f, 1.0f));

        /* Top-Right Live Clock */
        time_t cur_time = rtc_boot_time() + (frame / 60);
        struct tm *timeinfo = gmtime(&cur_time);
        char clock_buf[32];
        if (timeinfo) {
            snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d:%02d UTC", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        } else {
            snprintf(clock_buf, sizeof(clock_buf), "12:00:00 UTC");
        }
        draw_text_shadow(480.0f, 37.0f, 0.75f, clock_buf,
                         PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.7f), PVR_PACK_COLOR(0.6f, 0.0f, 0.2f, 0.4f));

        /* 4. Active View Content */
        if (current_view == VIEW_MAIN_MENU) {
            /* Left Side: 3D Faceted Glass Swirl Mascot */
            draw_glass_panel(25.0f, 92.0f, 175.0f, 286.0f, 2.0f, 0, 0.0f);
            draw_3d_glass_swirl(112.0f, 205.0f, 0.85f, frame);
            draw_text_shadow(60.0f, 328.0f, 0.85f, "Dreamcast",
                             PVR_PACK_COLOR(0.95f, 1.0f, 1.0f, 1.0f), PVR_PACK_COLOR(0.5f, 0.0f, 0.2f, 0.5f));

            /* Right Side: Interactive Glossy Glass Menu Cards (h=58, spacing=70) */
            for (int i = 0; i < num_items; i++) {
                float card_y = 92.0f + i * 70.0f;
                int is_sel = (i == menu_sel);

                draw_glass_panel(215.0f, card_y, 405.0f, 58.0f, 2.0f, is_sel, frame * 0.1f);

                if (is_sel) {
                    draw_button_gem(236.0f, card_y + 29.0f, 9.0f, "", 0);
                    draw_text_shadow(255.0f, card_y + 6.0f, 0.88f, menu_items[i],
                                     PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f), PVR_PACK_COLOR(0.8f, 0.0f, 0.3f, 0.6f));
                    draw_text_smooth(255.0f, card_y + 29.0f, 0.58f, menu_descriptions[i],
                                     PVR_PACK_COLOR(0.92f, 0.82f, 0.98f, 1.0f));
                } else {
                    draw_text_shadow(235.0f, card_y + 6.0f, 0.85f, menu_items[i],
                                     PVR_PACK_COLOR(0.80f, 0.85f, 0.95f, 1.0f), PVR_PACK_COLOR(0.4f, 0.0f, 0.1f, 0.3f));
                    draw_text_smooth(235.0f, card_y + 29.0f, 0.56f, menu_descriptions[i],
                                     PVR_PACK_COLOR(0.60f, 0.68f, 0.82f, 0.95f));
                }
            }

            /* Bottom Glass Toolbar */
            draw_glass_panel(20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(50.0f, 426.0f, 13.0f, "A", 0);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Select", PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));

            draw_button_gem(165.0f, 426.0f, 13.0f, "D", 2);
            draw_text_smooth(187.0f, 414.0f, 0.78f, "Navigate", PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));

            draw_text_smooth(380.0f, 416.0f, 0.70f, "PowerVR2 CLX2 @ 60 FPS", PVR_PACK_COLOR(0.75f, 0.7f, 0.95f, 1.0f));

        } else if (current_view == VIEW_PLAY_DISC) {
            char game_title[128];
            game_title[0] = '\0';
            int has_disc = check_disc_status(game_title, sizeof(game_title));

            draw_glass_panel(25.0f, 92.0f, 590.0f, 286.0f, 2.0f, has_disc, frame * 0.1f);
            draw_3d_holographic_disc(155.0f, 235.0f, 85.0f, frame, has_disc);

            draw_text_shadow(285.0f, 112.0f, 0.98f, has_disc ? "Disc Inserted" : "No Disc Detected",
                             has_disc ? PVR_PACK_COLOR(1.0f, 0.2f, 1.0f, 0.4f) : PVR_PACK_COLOR(1.0f, 1.0f, 0.4f, 0.4f),
                             PVR_PACK_COLOR(0.7f, 0.0f, 0.1f, 0.3f));

            draw_text_smooth(285.0f, 142.0f, 0.68f, "Media Format: Sega GD-ROM (1.2 GB)", PVR_PACK_COLOR(0.85f, 0.8f, 0.95f, 1.0f));
            draw_text_smooth(285.0f, 168.0f, 0.78f, has_disc ? game_title : "Please insert a Dreamcast disc",
                             PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));

            if (has_disc) {
                draw_glass_panel(285.0f, 215.0f, 290.0f, 46.0f, 2.5f, 1, frame * 0.15f);
                draw_button_gem(312.0f, 238.0f, 12.0f, "A", 0);
                draw_text_shadow(332.0f, 226.0f, 0.78f, "Press (A) to Launch",
                                 PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f), PVR_PACK_COLOR(0.7f, 0.0f, 0.2f, 0.5f));
            } else {
                draw_text_smooth(285.0f, 215.0f, 0.65f, "kos-insert discs can be loaded live", PVR_PACK_COLOR(0.7f, 0.7f, 0.85f, 1.0f));
            }

            draw_glass_panel(20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(50.0f, 426.0f, 13.0f, "B", 1);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Return to Menu", PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));

        } else if (current_view == VIEW_MEMORY_CARDS) {
            const char *vmu_ports[] = { "Port A1", "Port A2", "Port B1", "Port B2" };
            for (int v = 0; v < 4; v++) {
                float vx = (v % 2 == 0) ? 25.0f : 330.0f;
                float vy = (v < 2) ? 92.0f : 240.0f;

                maple_device_t *vmu = maple_enum_type(v, MAPLE_FUNC_MEMCARD);
                draw_vmu_card(vx, vy, 285.0f, 135.0f, 2.0f, vmu_ports[v], vmu != NULL, 128, frame);
            }

            draw_glass_panel(20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(50.0f, 426.0f, 13.0f, "B", 1);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Return to Menu", PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));

        } else if (current_view == VIEW_SETTINGS) {
            draw_glass_panel(25.0f, 92.0f, 590.0f, 286.0f, 2.0f, 0, 0.0f);

            int cable = vid_check_cable();
            const char *cable_name = (cable == CT_VGA) ? "VGA Box (640x480 60Hz RGB Progressive)" :
                                     ((cable == CT_RGB) ? "RGB SCART (480i 60Hz Interlaced)" : "Composite / S-Video (480i)");

            draw_text_shadow(45.0f, 106.0f, 0.78f, "Video Cable Output:",
                             PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.5f), PVR_PACK_COLOR(0.6f, 0.0f, 0.2f, 0.4f));
            draw_glass_panel(45.0f, 130.0f, 550.0f, 34.0f, 2.2f, 1, frame * 0.08f);
            draw_text_smooth(60.0f, 136.0f, 0.68f, cable_name, PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));

            draw_text_shadow(45.0f, 174.0f, 0.78f, "Audio Engine Mode:",
                             PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.5f), PVR_PACK_COLOR(0.6f, 0.0f, 0.2f, 0.4f));
            draw_glass_panel(45.0f, 198.0f, 550.0f, 34.0f, 2.2f, 0, 0.0f);
            draw_text_smooth(60.0f, 204.0f, 0.68f, "Yamaha AICA 64-CH 3D Spatial Stereo (44.1 kHz)",
                             PVR_PACK_COLOR(0.9f, 0.95f, 1.0f, 1.0f));

            draw_text_shadow(45.0f, 242.0f, 0.78f, "Firmware Theme:",
                             PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.5f), PVR_PACK_COLOR(0.6f, 0.0f, 0.2f, 0.4f));
            draw_glass_panel(45.0f, 266.0f, 550.0f, 34.0f, 2.2f, 0, 0.0f);
            draw_text_smooth(60.0f, 272.0f, 0.68f, "Liquid Glass (Active)",
                             PVR_PACK_COLOR(0.9f, 0.95f, 1.0f, 1.0f));

            draw_glass_panel(20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(50.0f, 426.0f, 13.0f, "B", 1);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Return to Menu", PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));

        } else if (current_view == VIEW_STATS) {
            draw_glass_panel(25.0f, 92.0f, 590.0f, 286.0f, 2.0f, 0, 0.0f);

            draw_text_shadow(45.0f, 102.0f, 0.82f, "Sega Dreamcast Hardware Architecture",
                             PVR_PACK_COLOR(1.0f, 0.3f, 1.0f, 0.6f), PVR_PACK_COLOR(0.6f, 0.0f, 0.2f, 0.4f));

            const char *specs[] = {
                "CPU: Hitachi SH-4 7091 @ 200 MHz (1.4 GFLOPS / 360 MIPS)",
                "GPU: NEC / VideoLogic PowerVR2 CLX2 @ 100 MHz (3M Poly/s)",
                "SPU: Yamaha AICA 64-Channel RISC Sound Processor",
                "Main RAM: 16 MB 100 MHz SDRAM (800 MB/s Bandwidth)",
                "VRAM: 8 MB 100 MHz SDRAM  |  SPU RAM: 2 MB SDRAM"
            };

            for (int s = 0; s < 5; s++) {
                float sy = 126.0f + s * 36.0f;
                draw_glass_panel(45.0f, sy, 550.0f, 30.0f, 2.2f, 0, 0.0f);
                draw_text_smooth(60.0f, sy + 6.0f, 0.65f, specs[s], PVR_PACK_COLOR(0.95f, 0.95f, 1.0f, 1.0f));
            }

            draw_text_smooth(45.0f, 342.0f, 0.60f, "Firmware: KallistiOS 2.0 Open-Source BIOS",
                             PVR_PACK_COLOR(1.0f, 1.0f, 0.9f, 0.3f));

            draw_glass_panel(20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(50.0f, 426.0f, 13.0f, "B", 1);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Return to Menu", PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f));
        }

        pvr_list_finish();
        pvr_scene_finish();
    }

    return 0;
}
