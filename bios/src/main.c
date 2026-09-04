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
#include "theme.h"

extern const uint8_t romdisk[];

/* Match DreamDash's known-good KOS startup requirements explicitly. */
KOS_INIT_FLAGS(INIT_IRQ | INIT_THD_PREEMPT | INIT_FS_ALL |
               INIT_LIBRARY | INIT_CDROM | INIT_CONTROLLER | INIT_VMU);

#define NUM_BUBBLES 24
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
    /* 69..74 (A4..D5)   */ 0x091C, 0x096A, 0x09BC, 0x0A13, 0x0AD2, 0x0AD2,
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

    /* 4. Synthesize 3 soothing 256-sample 16-bit PCM wavetables */
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

/* 20-Second Dynamic Multi-Theme Atmospheric Pad Sequencer */
static void aica_ambient_tick(const theme_t *theme, uint32_t frame) {
    uint32_t tick = frame % 1200;
    int step = tick / 300;
    int sub_tick = tick % 300;

    const theme_chord_step_t *chord = &theme->chords[step];

    if (sub_tick == 0) {
        aica_play_note(0, chord->sub_note,       14, 0x18, 0); /* Sub Bass Left */
        aica_play_note(1, chord->drone_note,     13, 0x08, 0); /* Drone Right   */
        aica_play_note(2, chord->pad_left_note,  11, 0x1C, 1); /* Pad Voice Left*/
        aica_play_note(3, chord->pad_right_note, 11, 0x04, 1); /* Pad Voice Right*/
        aica_play_note(4, chord->chime_note,     11, 0x1A, 2); /* Crystal Chime */
    } else if (sub_tick == 30) {
        aica_play_note(5, chord->chime_note,      8, 0x0E, 2); /* Echo Right */
    } else if (sub_tick == 60) {
        aica_play_note(6, chord->chime_note,      5, 0x00, 2); /* Diffuse Center */
    }
}

/* Floating Particle FX Data */
typedef struct {
    float x, y;
    float radius;
    float speed_y;
    float wobble_freq;
    float wobble_amp;
    float alpha;
} particle_t;

static particle_t particles[NUM_BUBBLES];

/* Expanding Water Ripple Data */
typedef struct {
    float x, y;
    float radius;
    float max_radius;
    float alpha;
    int active;
} ripple_t;

#include "logo_tex.h"

static ripple_t ripples[NUM_RIPPLES];

static pvr_ptr_t s_atlas_vram = NULL;
static pvr_ptr_t s_logo_vram = NULL;
static int s_current_logo_type = -1;

static void update_theme_logo(const theme_t *theme) {
    (void)theme;
    if (!s_logo_vram) return;
    int cur_idx = theme_get_current_index();
    int logo_type = (cur_idx == 1) ? 1 : ((cur_idx == 3) ? 2 : 0);
    if (logo_type != s_current_logo_type) {
        const uint16_t *src_tex = (logo_type == 1) ? LOGO_AERO_ORANGE :
                                  ((logo_type == 2) ? LOGO_AERO_CHROME : LOGO_AERO_AQUA);
        pvr_txr_load((void *)src_tex, s_logo_vram, 128 * 128 * 2);
        s_current_logo_type = logo_type;
    }
}

static void init_aero_environment(void) {
    s_atlas_vram = pvr_mem_malloc(256 * 256 * 2);
    if (s_atlas_vram) {
        pvr_txr_load((void *)AERO_ATLAS_TEX, s_atlas_vram, 256 * 256 * 2);
    }

    s_logo_vram = pvr_mem_malloc(128 * 128 * 2);
    if (s_logo_vram) {
        pvr_txr_load((void *)LOGO_AERO_AQUA, s_logo_vram, 128 * 128 * 2);
        s_current_logo_type = 0;
    }

    for (int i = 0; i < NUM_BUBBLES; i++) {
        particles[i].x = (float)(rand() % 640);
        particles[i].y = (float)(rand() % 500);
        particles[i].radius = 8.0f + (float)(rand() % 16);
        particles[i].speed_y = 0.35f + (float)(rand() % 100) * 0.008f;
        particles[i].wobble_freq = 0.02f + (float)(rand() % 50) * 0.001f;
        particles[i].wobble_amp = 6.0f + (float)(rand() % 14);
        particles[i].alpha = 0.45f + (float)(rand() % 45) * 0.01f;
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
    if (!s_atlas_vram || w <= 0.0f || h <= 0.0f) return;
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

static void draw_txr_logo_quad(float x, float y, float w, float h, float z, uint32_t col) {
    if (!s_logo_vram || w <= 0.0f || h <= 0.0f) return;
    pvr_poly_cxt_t cxt;
    pvr_poly_hdr_t hdr;
    pvr_vertex_t vert;

    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED,
                     128, 128, s_logo_vram, PVR_FILTER_BILINEAR);
    pvr_poly_compile(&hdr, &cxt);
    pvr_prim(&hdr, sizeof(hdr));

    vert.flags = PVR_CMD_VERTEX;
    vert.x = x; vert.y = y; vert.z = z;
    vert.u = 0.0f; vert.v = 0.0f;
    vert.argb = col; vert.oargb = 0;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x + w; vert.y = y;
    vert.u = 1.0f; vert.v = 0.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.x = x; vert.y = y + h;
    vert.u = 0.0f; vert.v = 1.0f;
    pvr_prim(&vert, sizeof(vert));

    vert.flags = PVR_CMD_VERTEX_EOL;
    vert.x = x + w; vert.y = y + h;
    vert.u = 1.0f; vert.v = 1.0f;
    pvr_prim(&vert, sizeof(vert));
}

static void draw_quad_gradient(float x, float y, float w, float h, float z,
                               uint32_t col_tl, uint32_t col_tr, uint32_t col_bl, uint32_t col_br) {
    if (w <= 0.0f || h <= 0.0f) return;
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

/* Mathematically Exact Proportional Typography */
static void draw_text_smooth(float start_x, float start_y, float scale, const char *text, uint32_t col) {
    if (!text || scale <= 0.02f) return;
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
   DYNAMIC THEMED GRAPHICS ENGINE
   ========================================================================= */

/* 1. Luminous Sky & Atmospheric Clouds */
static void draw_sky_atmosphere(const theme_t *theme, int frame) {
    /* Base Full-Screen Sky Gradient at z=0.40f (Furthest back in PVR camera depth) */
    draw_quad_gradient(0, 0, 640, 240, 0.40f, theme->sky_top, theme->sky_top, theme->sky_mid, theme->sky_mid);
    draw_quad_gradient(0, 240, 640, 240, 0.40f, theme->sky_mid, theme->sky_mid, theme->sky_bot, theme->sky_bot);

    /* Soft Atmospheric Cloud Billows — 3 slow-drifting wispy layers at z=0.45f */
    for (int layer = 0; layer < 3; layer++) {
        int num_wisps = 14;
        float cw = 640.0f / (float)num_wisps;
        float layer_y  = 50.0f + layer * 45.0f;
        float layer_spd = 0.004f + layer * 0.002f;
        float layer_a   = 0.22f - layer * 0.04f;

        for (int i = 0; i < num_wisps; i++) {
            float drift  = sinf(i * 0.72f + layer * 1.4f + frame * layer_spd) * 20.0f
                         + cosf(i * 0.28f - frame * layer_spd * 0.6f) * 12.0f;
            float drift2 = sinf((i+1)*0.72f + layer*1.4f + frame*layer_spd) * 20.0f
                         + cosf((i+1)*0.28f - frame*layer_spd*0.6f) * 12.0f;
            float x1 = i * cw, x2 = (i+1) * cw;
            float y1 = layer_y + drift;
            float y2 = layer_y + drift2;
            float h1 = 32.0f + sinf(i * 0.5f + frame * 0.003f) * 10.0f;
            float h2 = 32.0f + sinf((i+1) * 0.5f + frame * 0.003f) * 10.0f;

            uint32_t wisp_top = PVR_PACK_COLOR(layer_a, 0.94f, 0.96f, 1.0f);
            uint32_t wisp_bot = PVR_PACK_COLOR(0.0f,    0.85f, 0.90f, 1.0f);
            draw_quad_mesh(x1, y1-h1, x2, y2-h2, x1, y1+h1, x2, y2+h2, 0.45f,
                           wisp_top, wisp_top, wisp_bot, wisp_bot);
        }
    }

    /* Ambient celestial sun bloom at z=0.50f */
    float sun_x = 480.0f + sinf(frame * 0.008f) * 12.0f;
    float sun_y = 55.0f  + cosf(frame * 0.010f) * 8.0f;
    draw_quad_gradient(sun_x - 180, sun_y - 180, 360, 360, 0.50f,
                       theme->sun_fade, theme->sun_fade, theme->sun_fade, theme->sun_core);
}

/* 2. Authentic 3D Frutiger Aero Perspective Water Floor & Slow Organic Ripple Mesh */
static void draw_3d_water_environment(const theme_t *theme, int frame) {
    const int NUM_RINGS = 20;
    const int NUM_SECTORS = 32;
    const float CX = 320.0f;
    const float CY = 345.0f;
    const float MAX_RX = 325.0f;
    const float MAX_RY = 125.0f;

    float spd = theme->wave_speed;
    float amp = theme->wave_amplitude;
    float time_phase = (float)frame * 0.018f * spd;

    /* Precalculate ring vertices with 3D perspective wave elevation & dynamic caustics */
    typedef struct {
        float x, y;
        uint32_t col;
    } water_vert_t;

    water_vert_t grid[21][33];

    for (int r = 0; r <= NUM_RINGS; r++) {
        float tr = (float)r / (float)NUM_RINGS;
        float rx = tr * MAX_RX;
        float ry = tr * MAX_RY;
        float dist = tr * 260.0f;

        /* Slow propagating concentric water ripple rings */
        float wave1 = sinf(dist * 0.090f - time_phase);
        float wave2 = sinf(dist * 0.175f - time_phase * 1.6f) * 0.45f;
        float total_wave = wave1 + wave2;

        /* Wave crest elevation displacement */
        float center_well = expf(-tr * tr * 6.0f);
        float elev = total_wave * (1.0f - center_well * 0.5f) * 6.0f * amp;

        /* Dynamic Liquid Color & Caustic Specular Highlights */
        float base_a = (tr < 0.82f) ? (0.70f + total_wave * 0.18f) : (0.70f * (1.0f - tr) / 0.18f);
        if (base_a < 0.0f) base_a = 0.0f;
        if (base_a > 0.95f) base_a = 0.95f;

        /* Center pool: Deep azure blue -> outer radiant cyan caustics */
        float red = 0.08f + (1.0f - tr) * 0.15f + total_wave * 0.08f;
        float green = 0.42f + tr * 0.28f + total_wave * 0.22f;
        float blue = 0.78f + tr * 0.18f + total_wave * 0.14f;

        /* Specular sun glint on ripple crests */
        if (total_wave > 0.75f) {
            float glint = (total_wave - 0.75f) / 0.70f;
            red += glint * 0.45f;
            green += glint * 0.45f;
            blue += glint * 0.35f;
            base_a += glint * 0.20f;
        }

        if (red > 1.0f) red = 1.0f;
        if (green > 1.0f) green = 1.0f;
        if (blue > 1.0f) blue = 1.0f;
        if (base_a > 1.0f) base_a = 1.0f;

        uint32_t col = PVR_PACK_COLOR(base_a, red, green, blue);

        for (int s = 0; s <= NUM_SECTORS; s++) {
            float theta = ((float)s / (float)NUM_SECTORS) * (3.14159265f * 2.0f);
            float cos_t = cosf(theta);
            float sin_t = sinf(theta);

            /* Azimuthal caustic modulation */
            float az_wave = sinf(theta * 2.0f + dist * 0.05f - time_phase * 0.5f) * 2.0f;

            grid[r][s].x = CX + cos_t * rx;
            grid[r][s].y = CY + sin_t * ry + elev + az_wave * (tr * 0.8f);
            grid[r][s].col = col;
        }
    }

    /* Render 3D Water Surface Polygon Strips — z=0.75f sits behind UI panels at z=2.0f */
    for (int r = 0; r < NUM_RINGS; r++) {
        for (int s = 0; s < NUM_SECTORS; s++) {
            draw_quad_mesh(grid[r][s].x,     grid[r][s].y,
                           grid[r][s+1].x,   grid[r][s+1].y,
                           grid[r+1][s].x,   grid[r+1][s].y,
                           grid[r+1][s+1].x, grid[r+1][s+1].y,
                           0.75f,
                           grid[r][s].col,     grid[r][s+1].col,
                           grid[r+1][s].col,   grid[r+1][s+1].col);
        }
    }
}

/* 3. Multi-Theme Particle Dynamics */
static void draw_particles(const theme_t *theme, int frame) {
    float u0 = (32.0f - 26.0f) / 256.0f;
    float v0 = (200.0f - 26.0f) / 256.0f;
    float u1 = (32.0f + 26.0f) / 256.0f;
    float v1 = (200.0f + 26.0f) / 256.0f;

    for (int i = 0; i < NUM_BUBBLES; i++) {
        particles[i].y -= particles[i].speed_y * theme->particle_speed_mult;
        if (particles[i].y < -40.0f) {
            particles[i].y = 520.0f;
            particles[i].x = (float)(rand() % 640);
        }

        float wobble = sinf(particles[i].y * particles[i].wobble_freq + frame * 0.05f) * particles[i].wobble_amp;
        float bx = particles[i].x + wobble;
        float by = particles[i].y;
        float r = particles[i].radius;

        if (theme->particle_type == PARTICLE_CYBER_BITS) {
            /* Sharp rotating cyber motes */
            draw_quad_gradient(bx - r * 0.6f, by - r * 0.6f, r * 1.2f, r * 1.2f, 1.2f,
                               theme->particle_col, theme->particle_col,
                               PVR_PACK_COLOR(0.0f, 0.0f, 0.0f, 0.0f), theme->particle_col);
        } else if (theme->particle_type == PARTICLE_STARDUST) {
            /* Twinkling diamond stardust */
            float spark = fabsf(sinf(frame * 0.1f + i));
            float sr = r * (0.4f + spark * 0.6f);
            draw_quad(bx - sr, by - 1.0f, sr * 2.0f, 2.0f, 1.2f, theme->particle_col);
            draw_quad(bx - 1.0f, by - sr, 2.0f, sr * 2.0f, 1.2f, theme->particle_col);
        } else if (theme->particle_type == PARTICLE_PETALS) {
            /* Soft floating blossom petals */
            float sway = cosf(frame * 0.04f + i) * 3.0f;
            draw_quad_gradient(bx - r * 0.5f, by - r * 0.7f + sway, r, r * 1.4f, 1.2f,
                               theme->particle_col, theme->particle_col,
                               PVR_PACK_COLOR(0.3f, 1.0f, 0.5f, 0.7f), theme->particle_col);
        } else {
            /* PARTICLE_BUBBLES: Smooth translucent glass bubbles */
            draw_txr_quad(bx - r, by - r, r * 2.0f, r * 2.0f, 1.2f, u0, v0, u1, v1, theme->particle_col);
        }
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
static void draw_glass_panel(const theme_t *theme, float x, float y, float w, float h, float z, int is_selected, float glow_phase) {
    float glow_w = is_selected ? 6.0f : 2.0f;
    uint32_t c_glow = is_selected ? theme->glow_selected : theme->glow_unselected;
    draw_quad(x - glow_w, y - glow_w, w + glow_w * 2.0f, h + glow_w * 2.0f, z - 0.1f, c_glow);

    draw_quad_gradient(x, y, w, h, z, theme->panel_base_top, theme->panel_base_top, theme->panel_base_bot, theme->panel_base_bot);

    float glare_h = h * 0.48f;
    draw_quad_gradient(x + 2.0f, y + 2.0f, w - 4.0f, glare_h, z + 0.1f, theme->panel_glare_top, theme->panel_glare_top, theme->panel_glare_bot, theme->panel_glare_bot);

    uint32_t c_border_top = is_selected ? theme->border_selected : theme->panel_border_top;
    uint32_t c_border_bot = is_selected ? theme->glow_selected : theme->panel_border_bot;

    draw_quad(x, y, w, 1.5f, z + 0.2f, c_border_top);
    draw_quad(x, y, 1.5f, h, z + 0.2f, c_border_top);
    draw_quad(x, y + h - 1.5f, w, 1.5f, z + 0.2f, c_border_bot);
    draw_quad(x + w - 1.5f, y, 1.5f, h, z + 0.2f, c_border_bot);
}

/* 6a. Authentic Sega Dreamcast D-Pad Directional Cross Icon */
static void draw_dpad_icon(const theme_t *theme, float cx, float cy, float r) {
    float arm_w = r * 0.70f;
    float arm_l = r * 2.00f;
    float half_w = arm_w * 0.5f;
    float half_l = arm_l * 0.5f;

    /* Ambient backlight glow */
    draw_quad(cx - half_l - 2.0f, cy - half_w - 2.0f, arm_l + 4.0f, arm_w + 4.0f, 3.8f, theme->glow_unselected);
    draw_quad(cx - half_w - 2.0f, cy - half_l - 2.0f, arm_w + 4.0f, arm_l + 4.0f, 3.8f, theme->glow_unselected);

    /* Base glossy acrylic cross body */
    uint32_t c_base_top = theme->gem_x;
    uint32_t c_base_bot = theme->glow_selected;
    draw_quad_gradient(cx - half_l, cy - half_w, arm_l, arm_w, 4.0f, c_base_top, c_base_top, c_base_bot, c_base_bot);
    draw_quad_gradient(cx - half_w, cy - half_l, arm_w, arm_l, 4.0f, c_base_top, c_base_top, c_base_bot, c_base_bot);

    /* Specular glass bevel highlights */
    uint32_t c_rim = PVR_PACK_COLOR(0.85f, 0.95f, 1.0f, 1.0f);
    draw_quad(cx - half_l, cy - half_w, arm_l, 1.2f, 4.2f, c_rim);
    draw_quad(cx - half_w, cy - half_l, arm_w, 1.2f, 4.2f, c_rim);

    /* Center concave thumb dish */
    float dish_r = arm_w * 0.38f;
    draw_quad(cx - dish_r, cy - dish_r, dish_r * 2.0f, dish_r * 2.0f, 4.3f, PVR_PACK_COLOR(0.45f, 0.02f, 0.15f, 0.35f));

    /* Directional arrow notches (Up, Down, Left, Right) */
    float arr_s = 2.2f;
    draw_quad(cx - arr_s, cy - half_l + 2.0f, arr_s * 2.0f, 1.8f, 4.4f, 0xFFFFFFFF); /* Up */
    draw_quad(cx - arr_s, cy + half_l - 3.8f, arr_s * 2.0f, 1.8f, 4.4f, 0xFFFFFFFF); /* Down */
    draw_quad(cx - half_l + 2.0f, cy - arr_s, 1.8f, arr_s * 2.0f, 4.4f, 0xFFFFFFFF); /* Left */
    draw_quad(cx + half_l - 3.8f, cy - arr_s, 1.8f, arr_s * 2.0f, 4.4f, 0xFFFFFFFF); /* Right */
}

/* 6b. Smooth Round Glossy Controller Button Gems */
static void draw_button_gem(const theme_t *theme, float x, float y, float r, const char *label, int color_type) {
    float u0 = (160.0f - 24.0f) / 256.0f;
    float v0 = (200.0f - 24.0f) / 256.0f;
    float u1 = (160.0f + 24.0f) / 256.0f;
    float v1 = (200.0f + 24.0f) / 256.0f;

    uint32_t col;
    if (color_type == 0)      col = theme->gem_a;
    else if (color_type == 1) col = theme->gem_b;
    else if (color_type == 2) col = theme->gem_x;
    else if (color_type == 3) col = theme->gem_y;
    else                      col = theme->gem_selected;

    draw_txr_quad(x - r, y - r, r * 2.0f, r * 2.0f, 4.0f, u0, v0, u1, v1, col);

    /* Perfect center text inside circle */
    if (label && label[0]) {
        unsigned char c = (unsigned char)label[0];
        if (c >= 32 && c <= 126) {
            const glyph_t *g = &GLYPH_MAP[c - 32];
            float scale = (r * 1.35f) / 24.0f;
            float start_x = x - (float)g->adv * 0.5f * scale;
            float start_y = y - 16.0f * scale;
            draw_text_smooth(start_x, start_y, scale, label, 0xFFFFFFFF);
        }
    }
}

/* 7. Authentic 3D Frutiger Aero Translucent Glass Logo */
static void draw_3d_glass_swirl(const theme_t *theme, float center_x, float center_y, float scale, int frame) {
    if (scale <= 0.02f) return;
    update_theme_logo(theme);

    float float_y = sinf(frame * 0.035f) * 3.5f;
    float w = 112.0f * scale;
    float h = 100.0f * scale;
    float x = center_x - w * 0.5f;
    float y = center_y - h * 0.5f + float_y;

    /* Pulsing caustic glow aura around glass logo */
    float pulse = fabsf(sinf(frame * 0.03f));
    uint32_t glow_col = PVR_PACK_COLOR(0.20f + pulse * 0.25f, 0.4f, 0.8f, 1.0f);
    draw_txr_logo_quad(x - 3.0f, y - 3.0f, w + 6.0f, h + 6.0f, 3.0f, glow_col);

    /* 3D Translucent Glass Logo Quad */
    draw_txr_logo_quad(x, y, w, h, 3.5f, 0xFFFFFFFF);
}

/* 8. 3D Iridescent Holographic GD-ROM Disc */
static void draw_3d_holographic_disc(const theme_t *theme, float cx, float cy, float radius, int frame, int has_disc) {
    float rot = frame * 0.05f;

    draw_quad(cx - radius - 4, cy - radius * 0.6f - 4, (radius + 4) * 2.0f, (radius + 4) * 1.2f, 2.0f,
              PVR_PACK_COLOR(0.35f, 0.0f, 0.1f, 0.25f));

    draw_quad_gradient(cx - radius, cy - radius * 0.55f, radius * 2.0f, radius * 1.1f, 2.2f,
                       theme->disc_edge, theme->disc_body, theme->disc_body, theme->disc_edge);

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
    draw_quad(cx - hole_r, cy - hole_r * 0.55f, hole_r * 2.0f, hole_r * 1.1f, 2.7f, theme->sky_top);

    float glint_x = cx + cosf(rot * 2.0f) * (radius * 0.5f);
    float glint_y = cy + sinf(rot * 2.0f) * (radius * 0.28f);
    draw_quad(glint_x - 10, glint_y - 10, 20, 20, 2.8f, PVR_PACK_COLOR(0.85f, 1.0f, 1.0f, 1.0f));

    if (has_disc) {
        draw_glass_panel(theme, cx - 100, cy + radius * 0.65f, 200, 32, 3.0f, 1, frame * 0.1f);
        draw_text_shadow(cx - 75, cy + radius * 0.65f + 6.0f, 0.72f, "SEGA GD-ROM READY",
                         theme->text_accent, 0xAA000000);
    }
}

/* 9. Frosted Acrylic VMU Card Module */
static void draw_vmu_card(const theme_t *theme, float x, float y, float w, float h, float z, const char *port_name,
                          int is_inserted, int blocks_used, int frame) {
    draw_glass_panel(theme, x, y, w, h, z, is_inserted, frame * 0.08f);

    draw_text_shadow(x + 14.0f, y + 6.0f, 0.78f, port_name,
                     theme->text_title, theme->text_title_shadow);

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
        draw_text_smooth(x + 96.0f, y + 34.0f, 0.72f, buf, theme->text_accent);

        float bar_w = w - 110.0f;
        draw_quad(x + 96.0f, y + 56.0f, bar_w, 10.0f, z + 0.2f, PVR_PACK_COLOR(0.4f, 0.1f, 0.3f, 0.5f));
        float fill_w = (bar_w * (float)blocks_used) / 200.0f;
        draw_quad_gradient(x + 96.0f, y + 56.0f, fill_w, 10.0f, z + 0.3f,
                           theme->glow_selected, theme->glow_selected,
                           theme->panel_border_bot, theme->panel_border_bot);

        draw_text_smooth(x + 96.0f, y + 74.0f, 0.60f, "Status: Mounted & OK", theme->text_sub);
    } else {
        draw_text_smooth(lcd_x + 12.0f, lcd_y + 18.0f, 0.60f, "Offline", PVR_PACK_COLOR(0.6f, 0.2f, 0.35f, 0.2f));
        draw_text_smooth(x + 96.0f, y + 40.0f, 0.70f, "No VMU Inserted", theme->text_dim);
        draw_text_smooth(x + 96.0f, y + 66.0f, 0.58f, "Slot Empty", theme->text_dim);
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
    theme_init();
    const theme_t *theme = theme_get_current();
    pvr_set_bg_color(theme->bg_clear_r, theme->bg_clear_g, theme->bg_clear_b);
    init_aero_environment();

    /* Initialize Yamaha AICA Hardware SPU Ambient Synthesizer */
    aica_synth_init();

    int frame = 0;
    int menu_sel = 0;
    int settings_sel = 2; /* Default highlight to theme changer in settings */
    int current_view = VIEW_MAIN_MENU;
    uint32_t prev_buttons = 0;

    const char *menu_items[] = {
        "Play Disc",
        "File",
        "Settings",
        "Console Info"
    };
    const char *menu_descriptions[] = {
        "Boot inserted disc and launch Dreamcast game",
        "Inspect and manage VMU memory cards",
        "Configure video cables, audio output",
        "View system hardware"
    };
    int num_items = 4;

    while (1) {
        frame++;
        theme = theme_get_current();

        /* Advance Dynamic Atmospheric Ambient Pad Synthesizer */
        aica_ambient_tick(theme, frame);

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
                } else if (current_view == VIEW_SETTINGS) {
                    if (((st->buttons & CONT_DPAD_DOWN) || st->joyy > 40) &&
                        !((prev_buttons & CONT_DPAD_DOWN))) {
                        settings_sel = (settings_sel + 1) % 3;
                        trigger_ripple(320.0f, 130.0f + settings_sel * 68.0f + 17.0f);
                    }
                    if (((st->buttons & CONT_DPAD_UP) || st->joyy < -40) &&
                        !((prev_buttons & CONT_DPAD_UP))) {
                        settings_sel = (settings_sel - 1 + 3) % 3;
                        trigger_ripple(320.0f, 130.0f + settings_sel * 68.0f + 17.0f);
                    }
                    /* Live Theme Switching */
                    if (settings_sel == 2) {
                        if (((st->buttons & CONT_DPAD_RIGHT) || st->joyx > 40) &&
                            !((prev_buttons & CONT_DPAD_RIGHT))) {
                            theme_next();
                            theme = theme_get_current();
                            pvr_set_bg_color(theme->bg_clear_r, theme->bg_clear_g, theme->bg_clear_b);
                            trigger_ripple(320.0f, 266.0f);
                        }
                        if (((st->buttons & CONT_DPAD_LEFT) || st->joyx < -40) &&
                            !((prev_buttons & CONT_DPAD_LEFT))) {
                            theme_prev();
                            theme = theme_get_current();
                            pvr_set_bg_color(theme->bg_clear_r, theme->bg_clear_g, theme->bg_clear_b);
                            trigger_ripple(320.0f, 266.0f);
                        }
                        if ((st->buttons & CONT_A) && !(prev_buttons & CONT_A)) {
                            theme_next();
                            theme = theme_get_current();
                            pvr_set_bg_color(theme->bg_clear_r, theme->bg_clear_g, theme->bg_clear_b);
                            trigger_ripple(320.0f, 266.0f);
                        }
                    }
                    if ((st->buttons & CONT_B) && !(prev_buttons & CONT_B)) {
                        current_view = VIEW_MAIN_MENU;
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

        /* Calculate Seamless Morphing Transition from Bootloader */
        float intro_t = (frame < 50) ? sinf(((float)frame / 50.0f) * (3.14159265f * 0.5f)) : 1.0f;
        if (frame == 1) {
            trigger_ripple(320.0f, 175.0f);
        }

        /* PVR Frame Render Pass */
        pvr_wait_ready();
        pvr_scene_begin();
        pvr_list_begin(PVR_LIST_TR_POLY);

        /* 1. Atmospheric Sky, Clouds & 3D Perspective Water Ripple Floor */
        draw_sky_atmosphere(theme, frame);
        draw_3d_water_environment(theme, frame);

        /* 2. Floating Liquid Particles & Water Ripples */
        draw_particles(theme, frame);
        draw_ripples();

        /* 3. Top Navigation Glass Banner (Slides in smoothly from top) */
        float top_y = 16.0f - (1.0f - intro_t) * 90.0f;
        draw_glass_panel(theme, 20.0f, top_y, 600.0f, 66.0f, 2.0f, 0, 0.0f);
        draw_3d_glass_swirl(theme, 55.0f, top_y + 33.0f, 0.40f * intro_t, frame);

        /* Top Banner Typography */
        draw_text_shadow(95.0f, top_y + 8.0f, 1.00f, "OpenDC Dashboard",
                         theme->text_title, theme->text_title_shadow);
        draw_text_smooth(95.0f, top_y + 36.0f, 0.58f, "Sega Dreamcast",
                         theme->text_sub);

        /* Top-Right Live Clock */
        time_t cur_time = rtc_boot_time() + (frame / 60);
        struct tm *timeinfo = gmtime(&cur_time);
        char clock_buf[32];
        if (timeinfo) {
            snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d:%02d UTC", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
        } else {
            snprintf(clock_buf, sizeof(clock_buf), "12:00:00 UTC");
        }
        draw_text_shadow(480.0f, top_y + 21.0f, 0.75f, clock_buf,
                         theme->text_accent, theme->text_title_shadow);

        /* 4. Active View Content */
        if (current_view == VIEW_MAIN_MENU) {
            /* Left Side: 3D Faceted Glass Swirl Mascot & Morphing Swirl Glide */
            float mascot_x = 25.0f - (1.0f - intro_t) * 180.0f;
            draw_glass_panel(theme, mascot_x, 92.0f, 175.0f, 286.0f, 2.0f, 0, 0.0f);

            /* Seamless Morph: Swirl glides smoothly from (320, 175) down to (112, 205) */
            float swirl_cx = 320.0f + (112.0f - 320.0f) * intro_t;
            float swirl_cy = 175.0f + (205.0f - 175.0f) * intro_t;
            float swirl_scale = 1.0f + (0.85f - 1.0f) * intro_t;
            draw_3d_glass_swirl(theme, swirl_cx, swirl_cy, swirl_scale, frame);

            draw_text_shadow(mascot_x + 35.0f, 328.0f, 0.85f * intro_t, "Dreamcast",
                             theme->text_title, theme->text_title_shadow);

            /* Right Side: Interactive Glossy Glass Menu Cards (Staggered spring cascade) */
            for (int i = 0; i < num_items; i++) {
                float card_y = 92.0f + i * 70.0f;
                int is_sel = (i == menu_sel);

                float card_delay = (float)i * 6.0f;
                float card_t = (frame < (int)card_delay + 30) ?
                    sinf((fmaxf(0.0f, (float)frame - card_delay) / 30.0f) * (3.14159265f * 0.5f)) : 1.0f;
                float card_x = 215.0f + (1.0f - card_t) * 280.0f;

                draw_glass_panel(theme, card_x, card_y, 405.0f, 58.0f, 2.0f, is_sel, frame * 0.1f);

                if (is_sel) {
                    draw_button_gem(theme, card_x + 21.0f, card_y + 29.0f, 9.0f, "", 4);
                    draw_text_shadow(card_x + 40.0f, card_y + 6.0f, 0.88f, menu_items[i],
                                     theme->text_title, theme->text_title_shadow);
                    draw_text_smooth(card_x + 40.0f, card_y + 29.0f, 0.58f, menu_descriptions[i],
                                     theme->text_body);
                } else {
                    draw_text_shadow(card_x + 20.0f, card_y + 6.0f, 0.85f, menu_items[i],
                                     theme->text_dim, theme->text_title_shadow);
                    draw_text_smooth(card_x + 20.0f, card_y + 29.0f, 0.56f, menu_descriptions[i],
                                     theme->text_dim);
                }
            }

            /* Bottom Glass Toolbar (Slides up smoothly from bottom) */
            float bot_y = 390.0f + (1.0f - intro_t) * 100.0f;
            draw_glass_panel(theme, 20.0f, bot_y, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(theme, 50.0f, bot_y + 36.0f, 13.0f, "A", 0);
            draw_text_smooth(72.0f, bot_y + 24.0f, 0.78f, "Select", theme->text_title);

            draw_dpad_icon(theme, 168.0f, bot_y + 36.0f, 12.0f);
            draw_text_smooth(190.0f, bot_y + 24.0f, 0.78f, "Navigate", theme->text_title);

            draw_text_smooth(380.0f, bot_y + 26.0f, 0.70f, "BIOS 0.1B", theme->text_sub);

        } else if (current_view == VIEW_PLAY_DISC) {
            char game_title[128];
            game_title[0] = '\0';
            int has_disc = check_disc_status(game_title, sizeof(game_title));

            draw_glass_panel(theme, 25.0f, 92.0f, 590.0f, 286.0f, 2.0f, has_disc, frame * 0.1f);
            draw_3d_holographic_disc(theme, 155.0f, 235.0f, 85.0f, frame, has_disc);

            draw_text_shadow(285.0f, 112.0f, 0.98f, has_disc ? "Disc Inserted" : "No Disc Detected",
                             has_disc ? theme->text_accent : theme->text_dim,
                             theme->text_title_shadow);

            draw_text_smooth(285.0f, 142.0f, 0.68f, "Media Format: Sega GD-ROM (1.2 GB)", theme->text_sub);
            draw_text_smooth(285.0f, 168.0f, 0.78f, has_disc ? game_title : "Please insert a Dreamcast disc",
                             theme->text_title);

            if (has_disc) {
                draw_glass_panel(theme, 285.0f, 215.0f, 290.0f, 46.0f, 2.5f, 1, frame * 0.15f);
                draw_button_gem(theme, 312.0f, 238.0f, 12.0f, "A", 0);
                draw_text_shadow(332.0f, 226.0f, 0.78f, "Press (A) to Launch",
                                 theme->text_title, theme->text_title_shadow);
            } else {
                draw_text_smooth(285.0f, 215.0f, 0.65f, "kos-insert discs can be loaded live", theme->text_dim);
            }

            draw_glass_panel(theme, 20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(theme, 50.0f, 426.0f, 13.0f, "B", 1);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Return to Menu", theme->text_title);

        } else if (current_view == VIEW_MEMORY_CARDS) {
            const char *vmu_ports[] = { "Port A1", "Port A2", "Port B1", "Port B2" };
            for (int v = 0; v < 4; v++) {
                float vx = (v % 2 == 0) ? 25.0f : 330.0f;
                float vy = (v < 2) ? 92.0f : 240.0f;

                maple_device_t *vmu = maple_enum_type(v, MAPLE_FUNC_MEMCARD);
                draw_vmu_card(theme, vx, vy, 285.0f, 135.0f, 2.0f, vmu_ports[v], vmu != NULL, 128, frame);
            }

            draw_glass_panel(theme, 20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(theme, 50.0f, 426.0f, 13.0f, "B", 1);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Return to Menu", theme->text_title);

        } else if (current_view == VIEW_SETTINGS) {
            draw_glass_panel(theme, 25.0f, 92.0f, 590.0f, 286.0f, 2.0f, 0, 0.0f);

            int cable = vid_check_cable();
            const char *cable_name = (cable == CT_VGA) ? "VGA Box (640x480 60Hz RGB Progressive)" :
                                     ((cable == CT_RGB) ? "RGB SCART (480i 60Hz Interlaced)" : "Composite / S-Video (480i)");

            /* Row 0: Video Output */
            draw_text_shadow(45.0f, 106.0f, 0.78f, "Video Cable Output:",
                             theme->text_sub, theme->text_title_shadow);
            draw_glass_panel(theme, 45.0f, 130.0f, 550.0f, 34.0f, 2.2f, (settings_sel == 0), frame * 0.08f);
            draw_text_smooth(60.0f, 136.0f, 0.68f, cable_name, theme->text_title);

            /* Row 1: Audio Mode */
            draw_text_shadow(45.0f, 174.0f, 0.78f, "Audio Engine Mode:",
                             theme->text_sub, theme->text_title_shadow);
            draw_glass_panel(theme, 45.0f, 198.0f, 550.0f, 34.0f, 2.2f, (settings_sel == 1), 0.0f);
            draw_text_smooth(60.0f, 204.0f, 0.68f, "Yamaha AICA 64-CH 3D Spatial Stereo (44.1 kHz)",
                             theme->text_body);

            /* Row 2: Firmware Theme (Interactive Live Switcher) */
            char theme_title_buf[64];
            snprintf(theme_title_buf, sizeof(theme_title_buf), "Firmware Theme:  <  %s  >  (%d/%d)",
                     theme->name, theme_get_current_index() + 1, theme_get_count());
            draw_text_shadow(45.0f, 242.0f, 0.78f, theme_title_buf,
                             (settings_sel == 2) ? theme->text_accent : theme->text_sub, theme->text_title_shadow);

            draw_glass_panel(theme, 45.0f, 266.0f, 550.0f, 34.0f, 2.2f, (settings_sel == 2), frame * 0.12f);
            
            char tagline_buf[96];
            snprintf(tagline_buf, sizeof(tagline_buf), "%s  [Press Left/Right/A to Cycle Theme]", theme->tagline);
            draw_text_smooth(60.0f, 272.0f, 0.68f, tagline_buf, theme->text_title);

            /* Bottom Toolbar with Controls */
            draw_glass_panel(theme, 20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(theme, 50.0f, 426.0f, 13.0f, "B", 1);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Return to Menu", theme->text_title);

            draw_button_gem(theme, 220.0f, 426.0f, 13.0f, "D", 2);
            draw_text_smooth(242.0f, 414.0f, 0.78f, "Cycle Theme (< >)", theme->text_title);

            draw_button_gem(theme, 430.0f, 426.0f, 13.0f, "A", 0);
            draw_text_smooth(452.0f, 414.0f, 0.78f, "Next Theme", theme->text_title);

        } else if (current_view == VIEW_STATS) {
            draw_glass_panel(theme, 25.0f, 92.0f, 590.0f, 286.0f, 2.0f, 0, 0.0f);

            draw_text_shadow(45.0f, 102.0f, 0.82f, "Sega Dreamcast Hardware Architecture",
                             theme->text_accent, theme->text_title_shadow);

            const char *specs[] = {
                "CPU: Hitachi SH-4 7091 @ 200 MHz (1.4 GFLOPS / 360 MIPS)",
                "GPU: NEC / VideoLogic PowerVR2 CLX2 @ 100 MHz (3M Poly/s)",
                "SPU: Yamaha AICA 64-Channel RISC Sound Processor",
                "Main RAM: 16 MB 100 MHz SDRAM (800 MB/s Bandwidth)",
                "VRAM: 8 MB 100 MHz SDRAM  |  SPU RAM: 2 MB SDRAM"
            };

            for (int s = 0; s < 5; s++) {
                float sy = 126.0f + s * 36.0f;
                draw_glass_panel(theme, 45.0f, sy, 550.0f, 30.0f, 2.2f, 0, 0.0f);
                draw_text_smooth(60.0f, sy + 6.0f, 0.65f, specs[s], theme->text_title);
            }

            draw_text_smooth(45.0f, 342.0f, 0.60f, "Firmware: KallistiOS 2.0 Open-Source BIOS",
                             theme->text_sub);

            draw_glass_panel(theme, 20.0f, 390.0f, 600.0f, 72.0f, 2.0f, 0, 0.0f);
            draw_button_gem(theme, 50.0f, 426.0f, 13.0f, "B", 1);
            draw_text_smooth(72.0f, 414.0f, 0.78f, "Return to Menu", theme->text_title);
        }

        /* Soft Optical Glass Melting Wash from Bootloader */
        if (frame < 30) {
            float bloom_a = (1.0f - ((float)frame / 30.0f)) * 0.85f;
            draw_quad(0.0f, 0.0f, 640.0f, 480.0f, 6.0f,
                      PVR_PACK_COLOR(bloom_a, 0.97f, 0.98f, 1.0f));
        }

        pvr_list_finish();
        pvr_scene_finish();
    }

    return 0;
}
