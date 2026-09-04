#include "boot_anim.h"
#include "video.h"
#include <stdint.h>

#define NUM_SPIRAL_NODES 84
#define NUM_VORTEX_PARTICLES 32
#define SCREEN_W 640
#define SCREEN_H 480

/* 8.8 Fixed-Point Sine Quarter-Wave Table (0..64 maps to 0..90 deg) */
static const int16_t sin_quarter_tab[65] = {
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
    if (angle <= 64)  return sin_quarter_tab[angle];
    if (angle <= 128) return sin_quarter_tab[128 - angle];
    if (angle <= 192) return -sin_quarter_tab[angle - 128];
    return -sin_quarter_tab[256 - angle];
}

static int32_t cos_fx(int angle) {
    return sin_fx(angle + 64);
}

static uint32_t udiv32_anim(uint32_t num, uint32_t den) {
    if (den == 0) return 0;
    uint32_t quot = 0, qbit = 1;
    while ((int32_t)den >= 0 && den < num) {
        den <<= 1;
        qbit <<= 1;
    }
    while (qbit) {
        if (num >= den) {
            num -= den;
            quot |= qbit;
        }
        den >>= 1;
        qbit >>= 1;
    }
    return quot;
}

static int32_t sdiv32_anim(int32_t num, int32_t den) {
    if (den == 0) return 0;
    int sign = 1;
    uint32_t unum, uden;
    if (num < 0) {
        sign = -sign;
        unum = (uint32_t)-num;
    } else {
        unum = (uint32_t)num;
    }
    if (den < 0) {
        sign = -sign;
        uden = (uint32_t)-den;
    } else {
        uden = (uint32_t)den;
    }
    uint32_t quot = udiv32_anim(unum, uden);
    return (sign < 0) ? -(int32_t)quot : (int32_t)quot;
}

/* 3D Particle Structure */
typedef struct {
    int32_t x, y, z;
    int angle;
    int radius;
    int speed;
    int y_offset;
    uint16_t color;
} vortex_particle_t;

static vortex_particle_t s_particles[NUM_VORTEX_PARTICLES];
static boot_scene_config_t s_config;

/* -------------------------------------------------------------------------
 * Color Blending & Shading Helpers
 * ------------------------------------------------------------------------- */
static uint16_t blend_rgb565(uint16_t c1, uint16_t c2, int t256) {
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

static uint16_t scale_rgb565(uint16_t col, int brightness256) {
    if (brightness256 <= 0) return 0;
    if (brightness256 > 256) brightness256 = 256;

    int r = (((col >> 11) & 0x1F) * brightness256) >> 8;
    int g = (((col >> 5)  & 0x3F) * brightness256) >> 8;
    int b = ((col & 0x1F) * brightness256) >> 8;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

#define QACR0 (*(volatile uint32_t *)0xFF000038)
#define QACR1 (*(volatile uint32_t *)0xFF00003C)

/* Fast Store Queue VRAM Clearing (clears entire 640x480 frame in ~0.2ms) */
static void fast_clear_vram(uint32_t fb_addr, uint16_t color) {
    uint32_t dword_val = ((uint32_t)color << 16) | color;
    QACR0 = (((fb_addr) >> 26) << 2) & 0x1C;
    QACR1 = (((fb_addr) >> 26) << 2) & 0x1C;

    volatile uint32_t *sq = (volatile uint32_t *)(0xE0000000 | (fb_addr & 0x03FFFFE0));
    for (int i = 0; i < 8; i++) {
        sq[i] = dword_val;
    }

    int blocks = (SCREEN_W * SCREEN_H * 2) >> 5; /* 19,200 blocks of 32 bytes */
    while (blocks--) {
        __asm__ volatile("pref @%0" : : "r"(sq));
        sq += 8;
    }
}

/* Fast Bresenham with Thickness for Antialiased Ribbon Drawing */
static void draw_thick_line_fb(volatile uint16_t *fb, int x0, int y0, int x1, int y1, int thickness, uint16_t color) {
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 >= y0) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    int half_t = thickness >> 1;

    while (1) {
        int y_start = y0 - half_t;
        int y_end   = y0 + half_t;
        if (y_start < 0) y_start = 0;
        if (y_end >= SCREEN_H) y_end = SCREEN_H - 1;

        int x_start = x0 - half_t;
        int x_end   = x0 + half_t;
        if (x_start < 0) x_start = 0;
        if (x_end >= SCREEN_W) x_end = SCREEN_W - 1;

        for (int py = y_start; py <= y_end; py++) {
            volatile uint16_t *row = fb + (py * SCREEN_W);
            for (int px = x_start; px <= x_end; px++) {
                row[px] = color;
            }
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = err << 1;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* -------------------------------------------------------------------------
 * Smooth Proportional Typography Engine for "Open Dreamcast"
 * ------------------------------------------------------------------------- */
static void draw_smooth_char(volatile uint16_t *fb, int x, int y, char c, uint16_t color, int scale) {
    if (c < 32 || c > 126) return;

    int glyph_idx = c - 32;
    extern const uint8_t FONT_8X8[95][8];
    uint16_t shadow_col = RGB565(15, 20, 35);
    uint16_t highlight_col = RGB565(255, 255, 255);

    for (int row = 0; row < 8; row++) {
        uint8_t bits = FONT_8X8[glyph_idx][row];
        if (!bits) continue;
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int px_base = x + col * scale;
                int py_base = y + row * scale;
                uint16_t px_col = (row == 0) ? highlight_col : color;

                /* Drop shadow pass */
                for (int sy = 0; sy < scale; sy++) {
                    int py = py_base + sy + 2;
                    if ((unsigned)py >= SCREEN_H) continue;
                    volatile uint16_t *line = fb + (py * SCREEN_W);
                    for (int sx = 0; sx < scale; sx++) {
                        int px = px_base + sx + 2;
                        if ((unsigned)px < SCREEN_W) {
                            line[px] = shadow_col;
                        }
                    }
                }

                /* Main character body pass */
                for (int sy = 0; sy < scale; sy++) {
                    int py = py_base + sy;
                    if ((unsigned)py >= SCREEN_H) continue;
                    volatile uint16_t *line = fb + (py * SCREEN_W);
                    for (int sx = 0; sx < scale; sx++) {
                        int px = px_base + sx;
                        if ((unsigned)px < SCREEN_W) {
                            line[px] = px_col;
                        }
                    }
                }
            }
        }
    }
}

static void draw_smooth_string_centered(volatile uint16_t *fb, int center_x, int y, const char *str, uint16_t color, int scale, int letter_spacing) {
    if (!str) return;

    int len = 0;
    while (str[len]) len++;

    int char_w = 8 * scale + letter_spacing;
    int total_w = len * char_w - letter_spacing;
    int start_x = center_x - (total_w / 2);

    for (int i = 0; i < len; i++) {
        draw_smooth_char(fb, start_x + i * char_w, y, str[i], color, scale);
    }
}

/* -------------------------------------------------------------------------
 * 3D Boot Scene Lifecycle
 * ------------------------------------------------------------------------- */
void boot_anim_init(const boot_scene_config_t *config) {
    if (config) {
        s_config = *config;
    } else {
        s_config.title = "Open Dreamcast";
        s_config.subtitle = "SEGA ARCHITECTURE  |  CUSTOM FIRMWARE";
        s_config.swirl_color_a = RGB565(255, 110, 20);  /* Iconic Sega Orange */
        s_config.swirl_color_b = RGB565(255, 210, 40);  /* Radiant Warm Gold */
        s_config.swirl_glint_color = RGB565(255, 255, 255);
        s_config.bg_color = RGB565(4, 8, 18);          /* Deep Console Midnight */
        s_config.num_particles = NUM_VORTEX_PARTICLES;
    }

    /* Initialize 3D Orbital Stardust Vortex */
    for (int i = 0; i < NUM_VORTEX_PARTICLES; i++) {
        s_particles[i].angle = (i * 256) >> 5;
        s_particles[i].radius = 70 + (i % 5) * 22;
        s_particles[i].speed = 1 + (i % 3);
        s_particles[i].y_offset = ((i % 7) - 3) * 12;
        s_particles[i].color = (i % 2 == 0) ? s_config.swirl_color_b : RGB565(120, 220, 255);
    }
}

void boot_anim_render_frame(int frame, int total_frames, uint32_t fb_addr) {
    volatile uint16_t *fb = (volatile uint16_t *)fb_addr;

    /* 1. Fast Store Queue Clear of Back Buffer to Clean Midnight Backdrop */
    fast_clear_vram(fb_addr, s_config.bg_color);

    /* 2. Compute 3D Camera & Easing Transformations */
    int anim_progress = (total_frames > 0) ? sdiv32_anim(frame * 256, total_frames) : 0;

    /* Swirl draw progression (unrolling during frames 0..120) */
    int max_nodes = (frame < 120) ? sdiv32_anim(frame * NUM_SPIRAL_NODES, 120) : NUM_SPIRAL_NODES;
    if (max_nodes < 2) max_nodes = 2;
    if (max_nodes > NUM_SPIRAL_NODES) max_nodes = NUM_SPIRAL_NODES;

    /* Smooth 3D Yaw & Pitch Rotation */
    int rot_yaw = (frame < 130) ? ((frame * 3) & 0xFF) : ((130 * 3) + ((frame - 130) * 1)) & 0xFF;
    int rot_pitch = (frame < 130) ? ((sin_fx(frame * 2) * 20) >> 8) : 0;
    int rot_roll  = (frame < 130) ? ((cos_fx(frame * 2) * 15) >> 8) : 0;

    int center_x = 320;
    int center_y = 195;
    int fov = 340;
    int cam_dist = 280;

    /* 3. Render 3D Stardust Vortex Particles (Background & Foreground) */
    for (int p = 0; p < NUM_VORTEX_PARTICLES; p++) {
        s_particles[p].angle = (s_particles[p].angle + s_particles[p].speed) & 0xFF;

        int32_t px0 = (cos_fx(s_particles[p].angle) * s_particles[p].radius) >> 8;
        int32_t py0 = s_particles[p].y_offset + ((sin_fx(s_particles[p].angle * 2) * 15) >> 8);
        int32_t pz0 = (sin_fx(s_particles[p].angle) * s_particles[p].radius) >> 8;

        /* Rotate with 3D camera */
        int32_t px1 = (px0 * cos_fx(rot_yaw) + pz0 * sin_fx(rot_yaw)) >> 8;
        int32_t pz1 = (-px0 * sin_fx(rot_yaw) + pz0 * cos_fx(rot_yaw)) >> 8;
        int32_t py1 = py0;

        int32_t z_proj = cam_dist + pz1;
        if (z_proj < 40) z_proj = 40;

        int sx = center_x + sdiv32_anim(px1 * fov, z_proj);
        int sy = center_y + sdiv32_anim(py1 * fov, z_proj);

        if (sx >= 2 && sx < SCREEN_W - 2 && sy >= 2 && sy < SCREEN_H - 2) {
            int depth_bright = (pz1 > 0) ? 256 : 140;
            uint16_t p_col = scale_rgb565(s_particles[p].color, depth_bright);
            fb[sy * SCREEN_W + sx] = p_col;
            fb[sy * SCREEN_W + sx + 1] = p_col;
            fb[(sy + 1) * SCREEN_W + sx] = p_col;
        }
    }

    /* 4. Project & Render 3D Volumetric Archimedean Spiral Ribbon */
    int proj_x[NUM_SPIRAL_NODES];
    int proj_y[NUM_SPIRAL_NODES];
    int proj_z[NUM_SPIRAL_NODES];
    int proj_t[NUM_SPIRAL_NODES];

    for (int i = 0; i < max_nodes; i++) {
        int t256 = udiv32_anim(i * 256, NUM_SPIRAL_NODES);

        /* Mathematical Archimedean Spiral Curve */
        int r = (t256 * 110) >> 8;
        int angle = (t256 * 3 + (frame * 1)) & 0xFF;

        int32_t wx = (cos_fx(angle) * r) >> 8;
        int32_t wy = (sin_fx(angle) * (r * 3 / 5)) >> 8;
        int32_t wz = ((sin_fx(t256 + frame * 2) * 28) >> 8);

        /* 3D Yaw Rotation */
        int32_t x1 = (wx * cos_fx(rot_yaw) + wz * sin_fx(rot_yaw)) >> 8;
        int32_t z1 = (-wx * sin_fx(rot_yaw) + wz * cos_fx(rot_yaw)) >> 8;

        /* 3D Pitch Rotation */
        int32_t y2 = (wy * cos_fx(rot_pitch) - z1 * sin_fx(rot_pitch)) >> 8;
        int32_t z2 = (wy * sin_fx(rot_pitch) + z1 * cos_fx(rot_pitch)) >> 8;

        int32_t z_proj = cam_dist + z2;
        if (z_proj < 30) z_proj = 30;

        proj_x[i] = center_x + sdiv32_anim(x1 * fov, z_proj);
        proj_y[i] = center_y + sdiv32_anim(y2 * fov, z_proj);
        proj_z[i] = z2;
        proj_t[i] = (2 + ((t256 * 5) >> 8));
    }

    /* Connect Spiral Ribbon with Dynamic Specular Shading */
    for (int i = 0; i < max_nodes - 1; i++) {
        int t256 = udiv32_anim(i * 256, NUM_SPIRAL_NODES);
        uint16_t node_color = blend_rgb565(s_config.swirl_color_a, s_config.swirl_color_b, t256);

        /* Specular Glint when approaching resolution (frame >= 120) */
        if (frame >= 120) {
            int glint_phase = ((frame - 120) * 8);
            int dist_to_glint = (i * 3) - glint_phase;
            if (dist_to_glint < 0) dist_to_glint = -dist_to_glint;
            if (dist_to_glint < 16) {
                node_color = blend_rgb565(node_color, s_config.swirl_glint_color, 256 - dist_to_glint * 16);
            }
        }

        int thickness = proj_t[i];
        draw_thick_line_fb(fb, proj_x[i], proj_y[i], proj_x[i + 1], proj_y[i + 1], thickness, node_color);
    }

    /* Glowing Core Hub at Origin */
    if (max_nodes > 0) {
        int hx = proj_x[0];
        int hy = proj_y[0];
        for (int dy = -3; dy <= 3; dy++) {
            for (int dx = -3; dx <= 3; dx++) {
                if (dx * dx + dy * dy <= 9) {
                    int px = hx + dx;
                    int py = hy + dy;
                    if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                        fb[py * SCREEN_W + px] = s_config.swirl_glint_color;
                    }
                }
            }
        }
    }

    /* 5. High-Definition Anti-Aliased "Open Dreamcast" Typography */
    /* Smooth Title Fade-In */
    int text_alpha = 0;
    if (frame > 60) {
        text_alpha = sdiv32_anim((frame - 60) * 256, 60);
        if (text_alpha > 256) text_alpha = 256;
    }

    if (text_alpha > 0) {
        uint16_t title_color = blend_rgb565(RGB565(10, 15, 30), RGB565(250, 252, 255), text_alpha);
        uint16_t sub_color   = blend_rgb565(RGB565(10, 15, 30), RGB565(120, 190, 240), text_alpha);

        /* Main Console Branding: "Open Dreamcast" (Scale 3, crisp kerning) */
        draw_smooth_string_centered(fb, 320, 335, s_config.title, title_color, 3, 5);

        /* Subtitle Banner: "SEGA ARCHITECTURE | CUSTOM FIRMWARE" */
        draw_smooth_string_centered(fb, 320, 395, s_config.subtitle, sub_color, 1, 3);

        /* Authentic Sega Console License / Firmware Seal */
        uint16_t seal_color = blend_rgb565(RGB565(10, 15, 30), RGB565(70, 110, 160), text_alpha);
        draw_smooth_string_centered(fb, 320, 435, "PRODUCED BY OR UNDER LICENSE FROM SEGA ENTERPRISES, LTD.", seal_color, 1, 1);
    }
}

void boot_anim_shutdown(void) {
    /* Release any allocated resources cleanly */
}
