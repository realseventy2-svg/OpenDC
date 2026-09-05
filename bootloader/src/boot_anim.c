#include "boot_anim.h"
#include "video.h"
#include "logo_data.h"
#include "modern_assets.h"
#include "frost_map.h"
#include "math_fx.h"
#include <stdint.h>

#define SCREEN_W 640
#define SCREEN_H 480


static boot_scene_config_t s_config;


/* -------------------------------------------------------------------------
 * Ultra-Fast Color Blending & Shading Helpers (No VRAM readback needed)
 * ------------------------------------------------------------------------- */
static inline uint16_t blend_rgb565(uint16_t c1, uint16_t c2, int t256) {
    if (t256 <= 0)   return c1;
    if (t256 >= 256) return c2;

    int r1 = (c1 >> 11) & 0x1F;
    int g1 = (c1 >> 5)  & 0x3F;
    int b1 = c1 & 0x1F;

    int r2 = (c2 >> 11) & 0x1F;
    int g2 = (c2 >> 5)  & 0x3F;
    int b2 = c2 & 0x1F;

    int r = r1 + (((r2 - r1) * t256) >> 8);
    int g = g1 + (((g2 - g1) * t256) >> 8);
    int b = b1 + (((b2 - b1) * t256) >> 8);

    return (uint16_t)((r << 11) | (g << 5) | b);
}

#define QACR0 (*(volatile uint32_t *)0xFF000038)
#define QACR1 (*(volatile uint32_t *)0xFF00003C)

/* Ultra-Optimized Procedural Frosty Caustic Stream via SH-4 Store Queues */
static void render_realtime_frosty_caustic_bg(uint32_t fb_addr, int frame) {
    QACR0 = (((fb_addr) >> 26) << 2) & 0x1C;
    QACR1 = (((fb_addr) >> 26) << 2) & 0x1C;

    volatile uint32_t *sq = (volatile uint32_t *)(0xE0000000 | (fb_addr & 0x03FFFFE0));
    int shift_x = (frame >> 1) & 63;
    int shift_y = (frame >> 2) & 63;

    for (int y = 0; y < SCREEN_H; y++) {
        int base_v = 240 + ((y * 12) / SCREEN_H); /* 240..252 clean frosty white */
        int map_y = (y + shift_y) & 63;
        const uint8_t *frost_row = FROST_MAP + (map_y * 64);

        /* Precompute 64-pixel scanline pattern into L1 cache */
        uint16_t row_pat[64];
        for (int i = 0; i < 64; i++) {
            int map_x = (i + shift_x) & 63;
            int f_delta = (int)frost_row[map_x] - 128;
            int mod = f_delta >> 4; /* Fine crystal grain */

            int r = base_v + mod;
            int g = base_v + 2 + mod;
            int b = base_v + 5 + mod;
            if (r > 255) r = 255; else if (r < 0) r = 0;
            if (g > 255) g = 255; else if (g < 0) g = 0;
            if (b > 255) b = 255; else if (b < 0) b = 0;

            row_pat[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }

        /* Stream the 64-pixel pattern across the 640-pixel line via Store Queue bursts */
        uint32_t *p32 = (uint32_t *)row_pat;
        for (int rep = 0; rep < 10; rep++) {
            sq[0] = p32[0];  sq[1] = p32[1];  sq[2] = p32[2];  sq[3] = p32[3];
            sq[4] = p32[4];  sq[5] = p32[5];  sq[6] = p32[6];  sq[7] = p32[7];
            __asm__ volatile("pref @%0" : : "r"(sq));
            sq += 8;

            sq[0] = p32[8];  sq[1] = p32[9];  sq[2] = p32[10]; sq[3] = p32[11];
            sq[4] = p32[12]; sq[5] = p32[13]; sq[6] = p32[14]; sq[7] = p32[15];
            __asm__ volatile("pref @%0" : : "r"(sq));
            sq += 8;

            sq[0] = p32[16]; sq[1] = p32[17]; sq[2] = p32[18]; sq[3] = p32[19];
            sq[4] = p32[20]; sq[5] = p32[21]; sq[6] = p32[22]; sq[7] = p32[23];
            __asm__ volatile("pref @%0" : : "r"(sq));
            sq += 8;

            sq[0] = p32[24]; sq[1] = p32[25]; sq[2] = p32[26]; sq[3] = p32[27];
            sq[4] = p32[28]; sq[5] = p32[29]; sq[6] = p32[30]; sq[7] = p32[31];
            __asm__ volatile("pref @%0" : : "r"(sq));
            sq += 8;
        }
    }
}

/* -------------------------------------------------------------------------
 * High-Resolution Anti-Aliased Modern Typography & Branding Engine
 * ------------------------------------------------------------------------- */
static void draw_sega_badge_fb(volatile uint16_t *fb, int x, int y, int global_alpha) {
    if (global_alpha <= 0) return;
    uint16_t sega_blue = RGB565(0, 102, 204);
    uint16_t bg_col = RGB565(242, 245, 250);

    for (int py = 0; py < SEGA_BADGE_H; py++) {
        int dst_y = y + py;
        if (dst_y < 0 || dst_y >= SCREEN_H) continue;
        volatile uint16_t *line = fb + (dst_y * SCREEN_W);
        const uint8_t *row_packed = SEGA_BADGE_ALPHA_PACKED + (py * (SEGA_BADGE_W / 2));

        for (int px = 0; px < SEGA_BADGE_W; px += 2) {
            uint8_t byte_val = row_packed[px >> 1];
            uint8_t a1 = (byte_val >> 4) * 17;
            uint8_t a2 = (byte_val & 0x0F) * 17;

            int dst_x1 = x + px;
            if (a1 && dst_x1 >= 0 && dst_x1 < SCREEN_W) {
                int eff_a = (a1 * global_alpha) >> 8;
                line[dst_x1] = blend_rgb565(bg_col, sega_blue, eff_a);
            }

            int dst_x2 = x + px + 1;
            if (a2 && dst_x2 >= 0 && dst_x2 < SCREEN_W) {
                int eff_a = (a2 * global_alpha) >> 8;
                line[dst_x2] = blend_rgb565(bg_col, sega_blue, eff_a);
            }
        }
    }
}

static void draw_modern_title_fb(volatile uint16_t *fb, int cx, int cy, int global_alpha) {
    if (global_alpha <= 0) return;
    int start_x = cx - (TITLE_AA_W / 2);
    int start_y = cy - (TITLE_AA_H / 2);
    uint16_t title_color = RGB565(16, 26, 46);
    uint16_t bg_col = RGB565(248, 250, 254);

    for (int y = 0; y < TITLE_AA_H; y++) {
        int dst_y = start_y + y;
        if (dst_y < 0 || dst_y >= SCREEN_H) continue;
        volatile uint16_t *line = fb + (dst_y * SCREEN_W);
        const uint8_t *row_packed = TITLE_AA_PACKED + (y * (TITLE_AA_W / 2));

        for (int x = 0; x < TITLE_AA_W; x += 2) {
            uint8_t byte_val = row_packed[x >> 1];
            uint8_t a1 = (byte_val >> 4) * 17;
            uint8_t a2 = (byte_val & 0x0F) * 17;

            int dst_x1 = start_x + x;
            if (a1 && dst_x1 >= 0 && dst_x1 < SCREEN_W) {
                int eff_a = (a1 * global_alpha) >> 8;
                line[dst_x1] = blend_rgb565(bg_col, title_color, eff_a);
            }

            int dst_x2 = start_x + x + 1;
            if (a2 && dst_x2 >= 0 && dst_x2 < SCREEN_W) {
                int eff_a = (a2 * global_alpha) >> 8;
                line[dst_x2] = blend_rgb565(bg_col, title_color, eff_a);
            }
        }
    }
}

static void draw_modern_text(volatile uint16_t *fb, int x, int y, const char *str, uint16_t color, int global_alpha) {
    if (!str || global_alpha <= 0) return;
    uint16_t bg_col = RGB565(248, 250, 254);
    int cur_x = x;

    for (int i = 0; str[i]; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 32 || c > 126) c = ' ';
        int glyph_idx = c - 32;
        int adv = MODERN_GLYPH_ADV[glyph_idx];
        const uint8_t *glyph_data = MODERN_FONT_PACKED + (glyph_idx * (14 * 5));

        for (int gy = 0; gy < 14; gy++) {
            int dst_y = y + gy;
            if (dst_y < 0 || dst_y >= SCREEN_H) continue;
            volatile uint16_t *line = fb + (dst_y * SCREEN_W);
            const uint8_t *row = glyph_data + (gy * 5);

            for (int gx = 0; gx < 10; gx += 2) {
                uint8_t byte_val = row[gx >> 1];
                uint8_t a1 = (byte_val >> 4) * 17;
                uint8_t a2 = (byte_val & 0x0F) * 17;

                int dst_x1 = cur_x + gx;
                if (a1 && dst_x1 >= 0 && dst_x1 < SCREEN_W) {
                    int eff_a = (a1 * global_alpha) >> 8;
                    line[dst_x1] = blend_rgb565(bg_col, color, eff_a);
                }

                int dst_x2 = cur_x + gx + 1;
                if (a2 && dst_x2 >= 0 && dst_x2 < SCREEN_W) {
                    int eff_a = (a2 * global_alpha) >> 8;
                    line[dst_x2] = blend_rgb565(bg_col, color, eff_a);
                }
            }
        }
        cur_x += adv;
    }
}

static void draw_modern_text_centered(volatile uint16_t *fb, int center_x, int y, const char *str, uint16_t color, int global_alpha) {
    if (!str) return;
    int total_w = 0;
    for (int i = 0; str[i]; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 32 || c > 126) c = ' ';
        total_w += MODERN_GLYPH_ADV[c - 32];
    }
    draw_modern_text(fb, center_x - (total_w / 2), y, str, color, global_alpha);
}

/* -------------------------------------------------------------------------
 * Big 3D Frutiger Aero Translucent Glass Logo Rendering (112x112)
 * ------------------------------------------------------------------------- */
static void draw_glass_logo_fb(volatile uint16_t *fb, int cx, int cy, int global_alpha, int frame) {
    if (global_alpha <= 0) return;
    if (global_alpha > 256) global_alpha = 256;

    int half_w = BOOT_LOGO_W / 2;
    int half_h = BOOT_LOGO_H / 2;
    int start_x = cx - half_w;
    int start_y = cy - half_h;
    uint16_t bg_col = RGB565(246, 249, 253);

    /* Precompute Specular Sweep Position for this frame */
    int sweep = (frame >= 25) ? (((frame - 25) * 5) % (BOOT_LOGO_W + BOOT_LOGO_H + 40)) : -100;

    /* 112x112 3D Aqua Glass Logo Pixel Blending (Pure Write-Only, Zero VRAM Readback Latency) */
    for (int y = 0; y < BOOT_LOGO_H; y++) {
        int py = start_y + y;
        if (py < 0 || py >= SCREEN_H) continue;
        volatile uint16_t *dst_row = fb + (py * SCREEN_W);
        const uint8_t *pal_idx_row = BOOT_LOGO_PAL_INDEX + (y * BOOT_LOGO_W);
        const uint8_t *alpha_row = BOOT_LOGO_ALPHA_PACKED + (y * (BOOT_LOGO_W / 2));

        for (int x = 0; x < BOOT_LOGO_W; x += 2) {
            uint8_t byte_val = alpha_row[x >> 1];
            if (!byte_val) continue; /* Fast skip transparent blocks */

            uint8_t a1 = (byte_val >> 4) * 17;
            uint8_t a2 = (byte_val & 0x0F) * 17;

            /* Pixel 1 */
            if (a1) {
                int px1 = start_x + x;
                if (px1 >= 0 && px1 < SCREEN_W) {
                    int eff_a1 = (a1 * global_alpha) >> 8;
                    if (eff_a1 > 0) {
                        uint16_t c1 = BOOT_LOGO_PALETTE[pal_idx_row[x]];
                        if (sweep >= 0) {
                            int diag = (x + y) - sweep;
                            if (diag < 0) diag = -diag;
                            if (diag < 10) {
                                int glint = ((10 - diag) * 255) / 10;
                                c1 = blend_rgb565(c1, RGB565(255, 255, 255), (glint * global_alpha) >> 8);
                            }
                        }
                        dst_row[px1] = blend_rgb565(bg_col, c1, eff_a1);
                    }
                }
            }

            /* Pixel 2 */
            if (a2) {
                int px2 = start_x + x + 1;
                if (px2 >= 0 && px2 < SCREEN_W) {
                    int eff_a2 = (a2 * global_alpha) >> 8;
                    if (eff_a2 > 0) {
                        uint16_t c2 = BOOT_LOGO_PALETTE[pal_idx_row[x + 1]];
                        if (sweep >= 0) {
                            int diag = (x + 1 + y) - sweep;
                            if (diag < 0) diag = -diag;
                            if (diag < 10) {
                                int glint = ((10 - diag) * 255) / 10;
                                c2 = blend_rgb565(c2, RGB565(255, 255, 255), (glint * global_alpha) >> 8);
                            }
                        }
                        dst_row[px2] = blend_rgb565(bg_col, c2, eff_a2);
                    }
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Public Boot Animation Lifecycle API
 * ------------------------------------------------------------------------- */
void boot_anim_init(const boot_scene_config_t *config) {
    if (config) {
        s_config = *config;
    } else {
        s_config.title = "Open Dreamcast";
        s_config.subtitle = "SEGA DREAMCAST ARCHITECTURE";
        s_config.swirl_color_a = RGB565(30, 140, 230);  /* Deep Aqua Glass Cyan */
        s_config.swirl_color_b = RGB565(90, 210, 255);  /* Radiant Ice Cyan */
        s_config.swirl_glint_color = RGB565(255, 255, 255);
        s_config.bg_color = RGB565(248, 250, 254);      /* Frosty Ice White */
        s_config.num_particles = 0;
    }
}

void boot_anim_render_frame(int frame, int total_frames, uint32_t fb_addr) {
    volatile uint16_t *fb = (volatile uint16_t *)fb_addr;
    int center_x = 320;
    int center_y = 175;

    /* 1. Real-Time Procedural Frosty Caustic Background via SH-4 Store Queues */
    render_realtime_frosty_caustic_bg(fb_addr, frame);

    /* 2. Render Big 3D Aqua Glass Swirl Logo with Subtle Bob & Specular Caustics */
    int logo_alpha = 0;
    if (frame >= 3) {
        logo_alpha = sdiv32((frame - 3) * 256, 30);
        if (logo_alpha > 256) logo_alpha = 256;
    }
    int fade_start = total_frames - 90;
    if (fade_start < 0) fade_start = 0;
    if (frame > fade_start && total_frames > fade_start) {
        int fade_out = sdiv32((total_frames - frame) * 256, total_frames - fade_start);
        if (fade_out < 0) fade_out = 0;
        logo_alpha = (logo_alpha * fade_out) >> 8;
    }

    if (logo_alpha > 0) {
        int logo_y = center_y + ((sin_fx(frame * 4) * 3) >> 8);
        draw_glass_logo_fb(fb, center_x, logo_y, logo_alpha, frame);
    }

    /* 3. High-Definition Anti-Aliased Modern Typography & Branding */
    int text_alpha = 0;
    if (frame > 20) {
        text_alpha = sdiv32((frame - 20) * 256, 30);
        if (text_alpha > 256) text_alpha = 256;
    }
    if (frame > fade_start && total_frames > fade_start) {
        int fade_out = sdiv32((total_frames - frame) * 256, total_frames - fade_start);
        if (fade_out < 0) fade_out = 0;
        text_alpha = (text_alpha * fade_out) >> 8;
    }

    if (text_alpha > 0) {
        /* Top-Left: Crisp SEGA Brand Badge */
        draw_sega_badge_fb(fb, 36, 28, text_alpha);

        /* Main Console Title: "Open Dreamcast" (Anti-Aliased Modern Sans) */
        draw_modern_title_fb(fb, 320, 328, text_alpha);

        /* Subtitle Banner: "SEGA DREAMCAST ARCHITECTURE" (Charcoal Slate RGB 55, 75, 105) */
        uint16_t sub_col = RGB565(55, 75, 105);
        draw_modern_text_centered(fb, 320, 380, s_config.subtitle, sub_col, text_alpha);

        /* Bottom-Right: Modern Copyright Line */
        uint16_t copy_col = RGB565(90, 110, 135);
        draw_modern_text(fb, 395, 436, " ", copy_col, text_alpha); //sega enterprises
    }
}

void boot_anim_shutdown(void) {
    /* Release any allocated resources cleanly */
}
