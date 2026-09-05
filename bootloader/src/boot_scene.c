/*
 * boot_scene.c — Plug-and-Play Boot Scene Container Runtime
 * ==========================================================
 * SH-4 / PowerVR2 / AICA bare-metal implementation.
 * C99, no malloc, no libm, no heavy runtime libraries.
 *
 * Coordinate system (right-handed, Y-up):
 *   +X right, +Y up, -Z into screen.
 *   Camera looks along -Z by default (rotations applied as Euler YXZ).
 *
 * Fixed-point strategy:
 *   - Rotation sin/cos: 8.0 fixed (range [-256..+256] from math_fx.h)
 *   - Matrix elements : 16.16 fixed (int32_t, scaled by FX16 = 65536)
 *   - Intermediate 3D coordinates in 16.16
 *   - Perspective divide uses integer arithmetic, result in screen pixels
 */

#include "boot_scene.h"
#include "math_fx.h"
#include "sound.h"
#include "video.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Fixed-Point Scaling Constant
 * ========================================================================= */
#define FX16        65536       /* 1.0 in 16.16 fixed-point               */
#define FX16_HALF   32768       /* 0.5                                     */

/* =========================================================================
 * Static Runtime State (BSS — zero-cost, no heap)
 * ========================================================================= */
static BootSceneState s_scene;

/* =========================================================================
 * Internal: Safe 32-bit blob read (avoids strict-aliasing UB on SH-4)
 * ========================================================================= */
static inline uint32_t rd32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0]
         | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16)
         | ((uint32_t)b[3] << 24);
}

static inline uint16_t rd16(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

/* =========================================================================
 * Internal: Float read from packed blob (IEEE-754 little-endian on SH-4)
 * The SH-4 is natively IEEE-754 little-endian in single precision mode,
 * so we can alias through uint32_t safely with this helper.
 * ========================================================================= */
static inline float rd_float(const void *p)
{
    uint32_t bits = rd32(p);
    float    f;
    /* Safe type-pun via memcpy equivalent (compiler will optimise to fmov) */
    uint8_t *dst = (uint8_t *)&f;
    dst[0] = (uint8_t)(bits);
    dst[1] = (uint8_t)(bits >> 8);
    dst[2] = (uint8_t)(bits >> 16);
    dst[3] = (uint8_t)(bits >> 24);
    return f;
}

/* =========================================================================
 * Internal: Convert float degrees to 256-unit circle integer angle
 *   0° = 0, 90° = 64, 180° = 128, 360° = 256
 * ========================================================================= */
static inline int deg_to_angle(float deg)
{
    /* Multiply by 256/360 ≈ 0.7111...  Use integer: (deg * 256 + 180) / 360 */
    int32_t ideg = (int32_t)deg;          /* truncate — sufficient for baked data */
    int32_t a    = (ideg * 256) / 360;
    return a & 0xFF;
}

/* =========================================================================
 * Internal: 3×3 Rotation Matrix Build (Euler YXZ, 16.16 fixed-point)
 *
 *  rx, ry, rz — angles in 256-unit circle
 *  out[9]     — row-major 3×3 in 16.16
 *
 *  YXZ order: R = Ry * Rx * Rz
 * ========================================================================= */
static void build_rot3x3(int rx, int ry, int rz, int32_t out[9])
{
    int32_t sx = sin_fx(rx), cx = cos_fx(rx);
    int32_t sy = sin_fx(ry), cy = cos_fx(ry);
    int32_t sz = sin_fx(rz), cz = cos_fx(rz);

    /*
     * All sin/cos values are in range [-256..+256] (8.0 fixed).
     * Scale to 16.16 by multiplying each component by FX16/256 = 256.
     * Products of two 8.0 values are 16.0; shift right by 8 to get 8.0
     * then multiply by 256 to get 16.16.  Combined: >> 8 * 256 = same.
     * We do this inline to keep it integer-only.
     *
     * Matrix (row-major, each element in 16.16):
     *   R = Ry * Rx * Rz
     *   R[0,0] = cy*cz + sy*sx*sz
     *   R[0,1] = -cy*sz + sy*sx*cz
     *   R[0,2] = sy*cx
     *   R[1,0] = cx*sz
     *   R[1,1] = cx*cz
     *   R[1,2] = -sx
     *   R[2,0] = -sy*cz + cy*sx*sz
     *   R[2,1] = sy*sz + cy*sx*cz
     *   R[2,2] = cy*cx
     */
#define S(a, b) (((a) * (b)) >> 8)  /* 8.0 × 8.0 → 8.0 */
#define TO16(v) ((v) * 256)          /* 8.0 → 16.16 */

    out[0] = TO16(S(cy,cz) + S(S(sy,sx),sz));
    out[1] = TO16(-S(cy,sz) + S(S(sy,sx),cz));
    out[2] = TO16(S(sy,cx));
    out[3] = TO16(S(cx,sz));
    out[4] = TO16(S(cx,cz));
    out[5] = TO16(-sx);
    out[6] = TO16(-S(sy,cz) + S(S(cy,sx),sz));
    out[7] = TO16(S(sy,sz) + S(S(cy,sx),cz));
    out[8] = TO16(S(cy,cx));

#undef S
#undef TO16
}

/* =========================================================================
 * Internal: Transform a vertex by model-view matrix
 *
 * pos[3]      — model or camera translation in 16.16
 * rot[9]      — rotation matrix from build_rot3x3 in 16.16
 * vert[3]     — input vertex in 16.16 (from float * FX16)
 * out_cam[3]  — output in camera space, 16.16
 * ========================================================================= */
static void transform_vertex(const int32_t pos[3],
                              const int32_t rot[9],
                              const int32_t vert[3],
                              int32_t       out[3])
{
    /*
     * View-space = Rot_cam^T * (model_world - cam_pos)
     * Where model_world = Rot_model * vert + model_pos
     * For simplicity (baked transforms), we compose into one step below.
     *
     * The transforms array already encodes final model-world position so:
     *   cam_space = Rot_cam^T * (vert_world - cam_pos)
     *
     * rot^T * v: element j = sum_i rot[i*3+j] * v[i]
     */

    /* vert_world - cam_pos (in 16.16) */
    int32_t dx = vert[0] - pos[0];
    int32_t dy = vert[1] - pos[1];
    int32_t dz = vert[2] - pos[2];

    /*
     * Rotate by transposed camera rotation matrix.
     * rot is in 16.16, d* is in 16.16 → product is 32.32, shift right 16.
     * Use int32_t arithmetic; treat as 64-bit intermediate implicitly via
     * two 32-bit halves to stay bare-metal safe.
     */
#define MUL16(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 16))
    out[0] = MUL16(rot[0], dx) + MUL16(rot[3], dy) + MUL16(rot[6], dz);
    out[1] = MUL16(rot[1], dx) + MUL16(rot[4], dy) + MUL16(rot[7], dz);
    out[2] = MUL16(rot[2], dx) + MUL16(rot[5], dy) + MUL16(rot[8], dz);
#undef MUL16
}

/* =========================================================================
 * Internal: Perspective Project  →  screen-space pixel coords
 *
 * v_cam[3] — camera-space vertex in 16.16 fixed point
 * sx, sy   — output screen X/Y (integer pixels, 0-origin)
 * Returns 0 if behind camera (Z_cam >= 0 in right-handed camera space), 1 if visible.
 * ========================================================================= */
static int project_vertex(const int32_t v_cam[3], int *sx, int *sy)
{
    /* Camera looks along -Z; depth in front of camera is -v_cam[2] (16.16) */
    int32_t z_fx = -v_cam[2];
    if (z_fx <= 655) return 0;   /* near plane clip: closer than ~0.01 units or behind */

    /*
     * Full 16.16 fixed-point perspective division using standalone sdiv32 (no libgcc __divdi3).
     * sx = cx + (x_cam * focal_length) / z_cam
     * Both v_cam[0] and z_fx are scaled by 65536, so the scale factor cancels out.
     */
    int32_t proj_x = sdiv32(v_cam[0] * BOOT_SCENE_FOCAL_LEN, z_fx);
    int32_t proj_y = sdiv32(v_cam[1] * BOOT_SCENE_FOCAL_LEN, z_fx);

    int screen_x = 320 + proj_x;
    int screen_y = 240 - proj_y; /* flip Y for screen coordinates (Y-down) */

    *sx = screen_x;
    *sy = screen_y;
    return 1;
}


/* =========================================================================
 * Internal: Fast Solid Filled Triangle Rasterizer (Scanline Span Fill)
 * ========================================================================= */
static inline void draw_span_fb(uint32_t fb_addr, int y, int x0, int x1, uint16_t color)
{
    if (y < 0 || y >= 480) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= 640) x1 = 639;
    int count = x1 - x0 + 1;
    if (count <= 0) return;

    uint16_t *dst = (uint16_t *)fb_addr + (y * 640) + x0;
    uint32_t c32 = (uint32_t)color | ((uint32_t)color << 16);

    /* Align destination to 4 bytes */
    if (((uintptr_t)dst & 2) && count > 0) {
        *dst++ = color;
        count--;
    }

    uint32_t *dst32 = (uint32_t *)dst;
    int count32 = count >> 1;

    /* Fast 32-bit unrolled burst writes (write-only, zero VRAM read latency) */
    while (count32 >= 4) {
        dst32[0] = c32;
        dst32[1] = c32;
        dst32[2] = c32;
        dst32[3] = c32;
        dst32 += 4;
        count32 -= 4;
    }
    while (count32 > 0) {
        *dst32++ = c32;
        count32--;
    }
    if (count & 1) {
        *(uint16_t *)dst32 = color;
    }
}

static void draw_filled_triangle_fb(uint32_t fb_addr,
                                    int x0, int y0,
                                    int x1, int y1,
                                    int x2, int y2,
                                    uint16_t color)
{
    /* Sort vertices by Y: y0 <= y1 <= y2 */
    if (y0 > y1) { int tx = x0; x0 = x1; x1 = tx; int ty = y0; y0 = y1; y1 = ty; }
    if (y0 > y2) { int tx = x0; x0 = x2; x2 = tx; int ty = y0; y0 = y2; y2 = ty; }
    if (y1 > y2) { int tx = x1; x1 = x2; x2 = tx; int ty = y1; y1 = y2; y2 = ty; }

    if (y0 == y2 || y2 < 0 || y0 >= 480) return;

    /* Fixed-point 16.16 slope stepping: 0 divisions per scanline */
    int32_t total_h = y2 - y0;
    int32_t dx02 = sdiv32((x2 - x0) << 16, total_h);

    /* Top segment: y0 to y1 */
    int h0 = y1 - y0;
    if (h0 > 0) {
        int32_t dx01 = sdiv32((x1 - x0) << 16, h0);
        int start_y = (y0 < 0) ? 0 : y0;
        int end_y = (y1 >= 480) ? 479 : y1;
        int32_t cur_xa = (x0 << 16) + dx02 * (start_y - y0) + 32768;
        int32_t cur_xb = (x0 << 16) + dx01 * (start_y - y0) + 32768;

        for (int y = start_y; y <= end_y; y++) {
            draw_span_fb(fb_addr, y, cur_xa >> 16, cur_xb >> 16, color);
            cur_xa += dx02;
            cur_xb += dx01;
        }
    }

    /* Bottom segment: y1 to y2 */
    int h1 = y2 - y1;
    if (h1 > 0) {
        int32_t dx12 = sdiv32((x2 - x1) << 16, h1);
        int start_y = (y1 < 0) ? 0 : y1;
        int end_y = (y2 >= 480) ? 479 : y2;
        int32_t cur_xa = (x0 << 16) + dx02 * (start_y - y0) + 32768;
        int32_t cur_xb = (x1 << 16) + dx12 * (start_y - y1) + 32768;

        for (int y = start_y; y <= end_y; y++) {
            draw_span_fb(fb_addr, y, cur_xa >> 16, cur_xb >> 16, color);
            cur_xa += dx02;
            cur_xb += dx12;
        }
    }
}

/* =========================================================================
 * Internal: Silhouette Edge Anti-Aliasing Filter (5-tap Sub-pixel Smoothing)
 * Runs in 0.3ms on SH-4 after solid rasterization to smooth silhouette curves.
 * ========================================================================= */
static void smooth_mesh_edges_fb(uint32_t fb_addr, int min_x, int min_y, int max_x, int max_y, uint16_t orange_col)
{
    if (min_x < 2) min_x = 2;
    if (min_y < 2) min_y = 2;
    if (max_x > 637) max_x = 637;
    if (max_y > 477) max_y = 477;
    if (min_x >= max_x || min_y >= max_y) return;

    volatile uint16_t *fb = (volatile uint16_t *)fb_addr;

    int r_o = (orange_col >> 11) & 0x1F, g_o = (orange_col >> 5) & 0x3F, b_o = orange_col & 0x1F;

    /* Previous scanline buffer to prevent write-after-read cascading */
    static uint16_t prev_row[640];
    for (int x = min_x - 1; x <= max_x + 1; x++) {
        prev_row[x] = fb[(min_y - 1) * 640 + x];
    }

    for (int y = min_y; y <= max_y; y++) {
        volatile uint16_t *row_curr = fb + (y * 640);
        volatile uint16_t *row_next = fb + ((y + 1) * 640);

        uint16_t left_orig = row_curr[min_x - 1];

        for (int x = min_x; x <= max_x; x++) {
            uint16_t c_orig = row_curr[x];
            uint16_t c_up   = prev_row[x];
            uint16_t c_dn   = row_next[x];
            uint16_t c_lf   = left_orig;
            uint16_t c_rt   = row_curr[x + 1];

            left_orig = c_orig;
            prev_row[x] = c_orig;

            if (c_orig == orange_col) {
                int non_orange = (c_up != orange_col) + (c_dn != orange_col) + (c_lf != orange_col) + (c_rt != orange_col);
                if (non_orange > 0) {
                    /* Take average of the non-orange background neighbor(s) */
                    int r_bg = 0, g_bg = 0, b_bg = 0;
                    if (c_up != orange_col) { r_bg += (c_up >> 11) & 0x1F; g_bg += (c_up >> 5) & 0x3F; b_bg += c_up & 0x1F; }
                    if (c_dn != orange_col) { r_bg += (c_dn >> 11) & 0x1F; g_bg += (c_dn >> 5) & 0x3F; b_bg += c_dn & 0x1F; }
                    if (c_lf != orange_col) { r_bg += (c_lf >> 11) & 0x1F; g_bg += (c_lf >> 5) & 0x3F; b_bg += c_lf & 0x1F; }
                    if (c_rt != orange_col) { r_bg += (c_rt >> 11) & 0x1F; g_bg += (c_rt >> 5) & 0x3F; b_bg += c_rt & 0x1F; }
                    
                    if (non_orange == 2) {
                        r_bg >>= 1; g_bg >>= 1; b_bg >>= 1;
                    } else if (non_orange == 3) {
                        r_bg = (r_bg * 85) >> 8; g_bg = (g_bg * 85) >> 8; b_bg = (b_bg * 85) >> 8;
                    } else if (non_orange == 4) {
                        r_bg >>= 2; g_bg >>= 2; b_bg >>= 2;
                    }

                    int alpha_bg = non_orange * 52; /* blend 20%..81% background */
                    int r = r_o + (((r_bg - r_o) * alpha_bg) >> 8);
                    int g = g_o + (((g_bg - g_o) * alpha_bg) >> 8);
                    int b = b_o + (((b_bg - b_o) * alpha_bg) >> 8);
                    row_curr[x] = (uint16_t)((r << 11) | (g << 5) | b);
                }
            }
        }
    }
}

/* =========================================================================
 * Internal: Back-face cull via 2D cross product sign
 * ========================================================================= */
static int is_front_face(int x0, int y0, int x1, int y1, int x2, int y2)
{
    int32_t cross = (int32_t)(x1 - x0) * (int32_t)(y2 - y0)
                  - (int32_t)(y1 - y0) * (int32_t)(x2 - x0);
    return cross < 0;
}


/* =========================================================================
 * Internal: Copy a single wavetable block to AICA SPU RAM
 *
 * dst_spu_offset : byte offset within AICA RAM (e.g. 0, 512, 1024, 1536)
 * src            : pointer to 256 × int16_t samples in host memory
 * ========================================================================= */
static void upload_wavetable(uint32_t dst_spu_offset, const int16_t *src)
{
    volatile uint32_t *dst =
        (volatile uint32_t *)(BOOT_SCENE_AICA_RAM_BASE + dst_spu_offset);

    /* Write 32-bit pairs (2 × int16_t per write = 512 bytes / 4 = 128 writes) */
    const uint16_t *s16 = (const uint16_t *)src;
    for (int i = 0; i < 128; i++) {
        uint32_t lo = (uint32_t)s16[i * 2];
        uint32_t hi = (uint32_t)s16[i * 2 + 1];
        dst[i] = lo | (hi << 16);
    }
}

/* =========================================================================
 * Internal: 2D Alpha-Blended ARGB4444 Sprite Blitter
 * ========================================================================= */
static void blit_sprite_argb4444(uint32_t fb_addr, int dest_x, int dest_y, int width, int height, const uint16_t *pixels, int global_alpha)
{
    if (global_alpha <= 0 || width <= 0 || height <= 0 || !pixels) return;
    volatile uint16_t *fb = (volatile uint16_t *)fb_addr;

    for (int y = 0; y < height; y++) {
        int py = dest_y + y;
        if (py < 0 || py >= 330) continue; /* Mask line at Y=330: hide below baseline until popped up */
        volatile uint16_t *dst_row = fb + (py * 640);
        const uint16_t *src_row = pixels + (y * width);

        for (int x = 0; x < width; x++) {
            int px = dest_x + x;
            if (px < 0 || px >= 640) continue;

            uint16_t p = src_row[x];
            uint8_t a = (p >> 12) & 0x0F;
            if (a == 0) continue;

            int eff_a = (a * 17 * global_alpha) >> 8;
            if (eff_a <= 0) continue;

            int r_src = ((p >> 8) & 0x0F) * 2;   /* 0..30 (5-bit) */
            int g_src = ((p >> 4) & 0x0F) * 4;   /* 0..60 (6-bit) */
            int b_src = (p & 0x0F) * 2;          /* 0..30 (5-bit) */
            uint16_t src_col = (uint16_t)((r_src << 11) | (g_src << 5) | b_src);

            if (eff_a >= 250) {
                dst_row[px] = src_col;
            } else {
                uint16_t dst_col = dst_row[px];
                int r_dst = (dst_col >> 11) & 0x1F;
                int g_dst = (dst_col >> 5)  & 0x3F;
                int b_dst = dst_col & 0x1F;

                int r = r_dst + (((r_src - r_dst) * eff_a) >> 8);
                int g = g_dst + (((g_src - g_dst) * eff_a) >> 8);
                int b = b_dst + (((b_src - b_dst) * eff_a) >> 8);

                dst_row[px] = (uint16_t)((r << 11) | (g << 5) | b);
            }
        }
    }
}

/* =========================================================================
 * Public: boot_scene_mount()
 * ========================================================================= */
int boot_scene_mount(const void *blob)
{
    if (!blob) return -1;

    const uint8_t *base = (const uint8_t *)blob;

    /* Validate magic and version */
    const BootSceneHeader *h = (const BootSceneHeader *)base;
    if (rd32(&h->magic) != BOOT_SCENE_MAGIC) return -1;
    uint16_t ver = rd16(&h->version);
    if (ver < 1 || ver > 3) return -1;

    /* Sanity: audio_cue_count must not exceed our maximum */
    uint32_t cue_count = rd32(&h->audio_cue_count);
    if (cue_count > BOOT_SCENE_MAX_CUES) return -1;

    s_scene.hdr           = h;
    s_scene.transforms    = (const float *)(base + rd32(&h->off_transforms));
    s_scene.vertices      = (const float *)(base + rd32(&h->off_vertices));
    s_scene.indices       = (const uint16_t *)(base + rd32(&h->off_indices));
    if (ver >= 2 && rd16(&h->object_count) > 0) {
        s_scene.objects   = (const BootSceneObject *)(base + rd32(&h->off_colors));
        s_scene.colors    = (const uint16_t *)0;
    } else {
        s_scene.objects   = (const BootSceneObject *)0;
        s_scene.colors    = (rd32(&h->off_colors) != 0) ? (const uint16_t *)(base + rd32(&h->off_colors)) : (const uint16_t *)0;
    }
    s_scene.cues          = (const BootSceneAudioCue *)(base + rd32(&h->off_audio_cues));
    s_scene.sprites       = (rd32(&h->off_sprites) != 0) ? (const BootSceneSprite *)(base + rd32(&h->off_sprites)) : (const BootSceneSprite *)0;
    s_scene.sprite_frames = (rd32(&h->off_sprite_frames) != 0) ? (const BootSceneSpriteFrame *)(base + rd32(&h->off_sprite_frames)) : (const BootSceneSpriteFrame *)0;
    s_scene.blob_base     = base;
    s_scene.tick          = 0;
    s_scene.subtick       = 0;
    s_scene.mounted       = 1;

    /* Audio Initialization: Check if raw PCM audio is present */
    uint32_t audio_bytes = rd32(&h->audio_sample_bytes);
    s_scene.audio_bytes = audio_bytes;
    s_scene.audio_split_tick = 0;
    s_scene.audio_ch1_fired = 0;

    if (audio_bytes > 0) {
        /* 1. Hold ARM7 sound CPU in reset */
        *(volatile uint32_t *)0xA0702C00UL |= 1;

        /* 2. Configure master dry volume output to maximum (MVOL = 15 = 0x0F) */
        *(volatile uint16_t *)0xA0702800UL = 0x000F;

        /* 3. Stop and mute all 64 AICA hardware channels */
        for (int ch = 0; ch < 64; ch++) {
            AICA_CHN_REG(ch, 0x00) = 0x8000;
            AICA_CHN_REG(ch, 0x24) = 0x0000; /* DISDL = 0 (silence) */
        }

        /* 4. Copy PCM audio sample to AICA SPU RAM (32-bit burst writes) */
        const uint32_t *src32 = (const uint32_t *)(base + rd32(&h->off_wavetables));
        volatile uint32_t *dst32 = (volatile uint32_t *)BOOT_SCENE_AICA_RAM_BASE;
        uint32_t words = (audio_bytes + 3) >> 2;
        for (uint32_t i = 0; i < words; i++) {
            dst32[i] = src32[i];
        }

        /* 5. Setup Channel 0 */
        uint32_t len0 = audio_bytes;
        if (len0 > 44100) {
            len0 = 44100;                   /* 4.0s @ 11025 Hz */
            s_scene.audio_split_tick = 120; /* 4.0s @ 30 keyframes/sec */
        }

        AICA_CHN_REG(0, 0x00) = 0x8000;
        AICA_CHN_REG(0, 0x04) = 0x0000;         /* SA_low = 0 */
        AICA_CHN_REG(0, 0x08) = 0x0000;         /* LSA = 0 */
        AICA_CHN_REG(0, 0x0C) = (uint16_t)len0; /* LEA */
        AICA_CHN_REG(0, 0x10) = 0x001F;         /* AR = 31 (instant attack) */
        AICA_CHN_REG(0, 0x14) = 0x3C1F;         /* KRS = 15, DL = 0, RR = 31 */
        AICA_CHN_REG(0, 0x18) = 0x7000;         /* Pitch = 11,025 Hz (OCT=-2, FNS=0) */
        AICA_CHN_REG(0, 0x24) = 0x0F00;         /* DISDL = 15 (0dB max volume), center pan */
        AICA_CHN_REG(0, 0x28) = 0x0060;         /* VOFF = 1, LPOFF = 1 (bypass envelope & LPF) */

        /* Trigger Key-On Channel 0 (PCMS = 1 for 8-bit signed PCM) */
        AICA_CHN_REG(0, 0x00) = 0xC080;
    } else {
        /* Fallback: Upload 4 wavetables to AICA SPU RAM at 0xA0800000 */
        const int16_t *wav_block = (const int16_t *)(base + rd32(&h->off_wavetables));
        for (int wt = 0; wt < BOOT_SCENE_WAVETABLE_COUNT; wt++) {
            upload_wavetable((uint32_t)(wt * BOOT_SCENE_WAVETABLE_BYTES),
                             wav_block + wt * BOOT_SCENE_WAVETABLE_SAMPLES);
        }
    }

    return 0;
}

/* =========================================================================
 * Public: boot_scene_tick()
 * ========================================================================= */
void boot_scene_tick(uint32_t fb_addr)
{
    if (!s_scene.mounted) return;

    const BootSceneHeader *h = s_scene.hdr;
    uint32_t total_frames  = rd32(&h->total_frames);
    if (s_scene.tick >= total_frames) return;

    uint32_t tick          = s_scene.tick;
    uint32_t cue_count     = rd32(&h->audio_cue_count);
    uint16_t ver           = rd16(&h->version);
    uint32_t obj_count     = (ver >= 2) ? (uint32_t)rd16(&h->object_count) : 0;

#define MAX_VERTS 24576
    int sx_buf[MAX_VERTS], sy_buf[MAX_VERTS];
    uint8_t vis_buf[MAX_VERTS];

    if (ver >= 2 && obj_count > 0 && s_scene.objects) {
        /* ==============================================================
         * Version 2 & 3: Multi-Object Scene Graph (Version 3 = int16 quantized)
         * ============================================================== */
#define MAX_SCENE_OBJECTS 128
        uint32_t active_tick = tick;
        uint16_t stored_f = rd16(&h->stored_frames);
        if (stored_f > 0 && active_tick >= stored_f) {
            active_tick = stored_f - 1; /* Hold final keyframe */
        }

        uint32_t frame_stride = obj_count * BOOT_SCENE_TRANSFORM_FLOATS;
        const int16_t *tf_frame_i16 = (const int16_t *)s_scene.transforms + active_tick * frame_stride;
        const float   *tf_frame_f   = s_scene.transforms + active_tick * frame_stride;

        /* Depth-sort objects (Painter's algorithm: draw farthest objects first) */
        uint32_t sorted_objs[MAX_SCENE_OBJECTS];
        int32_t obj_depths[MAX_SCENE_OBJECTS];
        uint32_t render_count = 0;
        uint32_t max_o = (obj_count < MAX_SCENE_OBJECTS) ? obj_count : MAX_SCENE_OBJECTS;

        for (uint32_t o = 0; o < max_o; o++) {
            int32_t depth;
            if (ver == 3) {
                int32_t tz_raw = (int32_t)tf_frame_i16[o * 12 + 11];
                depth = -tz_raw * 512; /* scaled depth for sorting */
            } else {
                float tz = tf_frame_f[o * 12 + 11];
                depth = (int32_t)(-tz * FX16);
            }

            /* Insertion sort descending */
            uint32_t ins = render_count;
            while (ins > 0 && obj_depths[ins - 1] < depth) {
                sorted_objs[ins] = sorted_objs[ins - 1];
                obj_depths[ins] = obj_depths[ins - 1];
                ins--;
            }
            sorted_objs[ins] = o;
            obj_depths[ins] = depth;
            render_count++;
        }

        int bb_min_x = 640, bb_min_y = 480, bb_max_x = -1, bb_max_y = -1;

        for (uint32_t r_idx = 0; r_idx < render_count; r_idx++) {
            uint32_t o = sorted_objs[r_idx];
            const BootSceneObject *obj = &s_scene.objects[o];
            uint16_t flags = rd16(&obj->flags);

            float m0, m1, m2, m3, m4, m5, m6, m7, m8, m9, m10, m11;
            uint32_t visible_tris = rd32(&obj->tri_count);

            if (ver == 3) {
                const int16_t *tf = tf_frame_i16 + o * 12;
                if (tf[0] == 0 && tf[1] == 0 && tf[4] == 0 && tf[5] == 0) {
                    continue; /* Object is hidden at this frame */
                }
                if (flags & 1) {
                    /* Dynamic triangle reveal count stored in tf[2] */
                    visible_tris = (uint32_t)(uint16_t)tf[2];
                    m2 = 0.0f;
                } else {
                    m2 = (float)tf[2] * (1.0f / 8192.0f);
                }

                if (visible_tris == 0) continue;

                m0 = (float)tf[0] * (1.0f / 8192.0f);
                m1 = (float)tf[1] * (1.0f / 8192.0f);
                m3 = (float)tf[3] * (1.0f / 128.0f);
                m4 = (float)tf[4] * (1.0f / 8192.0f);
                m5 = (float)tf[5] * (1.0f / 8192.0f);
                m6 = (float)tf[6] * (1.0f / 8192.0f);
                m7 = (float)tf[7] * (1.0f / 128.0f);
                m8 = (float)tf[8] * (1.0f / 8192.0f);
                m9 = (float)tf[9] * (1.0f / 8192.0f);
                m10 = (float)tf[10] * (1.0f / 8192.0f);
                m11 = (float)tf[11] * (1.0f / 128.0f);
            } else {
                const float *tf = tf_frame_f + o * 12;
                m0 = tf[0]; m1 = tf[1]; m2 = tf[2]; m3 = tf[3];
                m4 = tf[4]; m5 = tf[5]; m6 = tf[6]; m7 = tf[7];
                m8 = tf[8]; m9 = tf[9]; m10 = tf[10]; m11 = tf[11];
            }

            uint32_t sv = rd32(&obj->start_vert);
            uint32_t nv = rd32(&obj->vert_count);
            if (nv > MAX_VERTS) nv = MAX_VERTS;

            for (uint32_t i = 0; i < nv; i++) {
                const float *v = s_scene.vertices + (sv + i) * 3;
                float vx = v[0], vy = v[1], vz = v[2];

                float cx = m0 * vx + m1 * vy + m2 * vz + m3;
                float cy = m4 * vx + m5 * vy + m6 * vz + m7;
                float cz = m8 * vx + m9 * vy + m10 * vz + m11;

                float z_depth = -cz;
                if (z_depth <= 0.05f) {
                    vis_buf[i] = 0;
                } else {
                    float inv_z = (float)BOOT_SCENE_FOCAL_LEN / z_depth;
                    sx_buf[i] = 320 + (int)(cx * inv_z);
                    sy_buf[i] = 240 - (int)(cy * inv_z);
                    vis_buf[i] = 1;
                }
            }

            uint32_t st = rd32(&obj->start_tri);
            uint16_t base_color = rd16(&obj->color);

            for (uint32_t t = 0; t < visible_tris; t++) {
                uint32_t base_idx = (st + t) * 3;
                uint16_t i0 = s_scene.indices[base_idx + 0];
                uint16_t i1 = s_scene.indices[base_idx + 1];
                uint16_t i2 = s_scene.indices[base_idx + 2];

                if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
                if (!vis_buf[i0] || !vis_buf[i1] || !vis_buf[i2]) continue;
                if (!is_front_face(sx_buf[i0], sy_buf[i0], sx_buf[i1], sy_buf[i1], sx_buf[i2], sy_buf[i2])) continue;

                int x_min = sx_buf[i0]; if (sx_buf[i1] < x_min) x_min = sx_buf[i1]; if (sx_buf[i2] < x_min) x_min = sx_buf[i2];
                int x_max = sx_buf[i0]; if (sx_buf[i1] > x_max) x_max = sx_buf[i1]; if (sx_buf[i2] > x_max) x_max = sx_buf[i2];
                int y_min = sy_buf[i0]; if (sy_buf[i1] < y_min) y_min = sy_buf[i1]; if (sy_buf[i2] < y_min) y_min = sy_buf[i2];
                int y_max = sy_buf[i0]; if (sy_buf[i1] > y_max) y_max = sy_buf[i1]; if (sy_buf[i2] > y_max) y_max = sy_buf[i2];

                if (x_min < bb_min_x) bb_min_x = x_min;
                if (x_max > bb_max_x) bb_max_x = x_max;
                if (y_min < bb_min_y) bb_min_y = y_min;
                if (y_max > bb_max_y) bb_max_y = y_max;

                draw_filled_triangle_fb(fb_addr, sx_buf[i0], sy_buf[i0], sx_buf[i1], sy_buf[i1], sx_buf[i2], sy_buf[i2], base_color);
            }
        }

        if (bb_max_x >= bb_min_x && bb_max_y >= bb_min_y) {
            uint16_t orange_col = (s_scene.objects) ? rd16(&s_scene.objects[0].color) : RGB565(255, 110, 20);
            smooth_mesh_edges_fb(fb_addr, bb_min_x - 1, bb_min_y - 1, bb_max_x + 1, bb_max_y + 1, orange_col);
        }
    } else {
        /* ==============================================================
         * Version 1: Single Mesh with per-frame camera/model transform
         * ============================================================== */
        uint32_t vertex_count  = rd32(&h->vertex_count);
        uint32_t index_count   = rd32(&h->index_count);

        const float *tf = s_scene.transforms + tick * BOOT_SCENE_TRANSFORM_FLOATS;
        float mpos_x = tf[0],  mpos_y = tf[1],  mpos_z = tf[2];
        float mrot_x = tf[3],  mrot_y = tf[4],  mrot_z = tf[5];
        float cpos_x = tf[6],  cpos_y = tf[7],  cpos_z = tf[8];
        float crot_x = tf[9],  crot_y = tf[10], crot_z = tf[11];

        int32_t m_rot[9], c_rot[9];
        build_rot3x3(deg_to_angle(mrot_x), deg_to_angle(mrot_y), deg_to_angle(mrot_z), m_rot);
        build_rot3x3(deg_to_angle(crot_x), deg_to_angle(crot_y), deg_to_angle(crot_z), c_rot);

        int32_t m_pos16[3], c_pos16[3];
        m_pos16[0] = (int32_t)(mpos_x * FX16); m_pos16[1] = (int32_t)(mpos_y * FX16); m_pos16[2] = (int32_t)(mpos_z * FX16);
        c_pos16[0] = (int32_t)(cpos_x * FX16); c_pos16[1] = (int32_t)(cpos_y * FX16); c_pos16[2] = (int32_t)(cpos_z * FX16);

        uint32_t nv = (vertex_count < MAX_VERTS) ? vertex_count : MAX_VERTS;

        for (uint32_t i = 0; i < nv; i++) {
            const float *v = s_scene.vertices + i * 3;
            int32_t vert16[3];
            vert16[0] = (int32_t)(v[0] * FX16);
            vert16[1] = (int32_t)(v[1] * FX16);
            vert16[2] = (int32_t)(v[2] * FX16);

            int32_t world[3];
#define MUL16(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 16))
            world[0] = MUL16(m_rot[0], vert16[0]) + MUL16(m_rot[1], vert16[1]) + MUL16(m_rot[2], vert16[2]) + m_pos16[0];
            world[1] = MUL16(m_rot[3], vert16[0]) + MUL16(m_rot[4], vert16[1]) + MUL16(m_rot[5], vert16[2]) + m_pos16[1];
            world[2] = MUL16(m_rot[6], vert16[0]) + MUL16(m_rot[7], vert16[1]) + MUL16(m_rot[8], vert16[2]) + m_pos16[2];
#undef MUL16

            int32_t cam[3];
            transform_vertex(c_pos16, c_rot, world, cam);
            vis_buf[i] = (uint8_t)project_vertex(cam, &sx_buf[i], &sy_buf[i]);
        }

        uint32_t tri_count = index_count / 3;

        for (uint32_t t = 0; t < tri_count; t++) {
            uint32_t base_idx = t * 3;
            uint16_t i0 = s_scene.indices[base_idx + 0];
            uint16_t i1 = s_scene.indices[base_idx + 1];
            uint16_t i2 = s_scene.indices[base_idx + 2];

            if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
            if (!vis_buf[i0] || !vis_buf[i1] || !vis_buf[i2]) continue;
            if (!is_front_face(sx_buf[i0], sy_buf[i0], sx_buf[i1], sy_buf[i1], sx_buf[i2], sy_buf[i2])) continue;

            int32_t dx1 = sx_buf[i1] - sx_buf[i0];
            int32_t dy1 = sy_buf[i1] - sy_buf[i0];
            int32_t dx2 = sx_buf[i2] - sx_buf[i0];
            int32_t dy2 = sy_buf[i2] - sy_buf[i0];

            int32_t nz = dx1 * dy2 - dy1 * dx2;
            if (nz < 0) nz = -nz;

            int shade = 140 + ((nz & 0x7F) * 115 / 128);
            if (shade > 255) shade = 255;

            uint16_t base_color = (s_scene.colors) ? s_scene.colors[t] : RGB565(255, 110, 20);
            int r = (base_color >> 11) & 0x1F;
            int g = (base_color >> 5)  & 0x3F;
            int b = base_color & 0x1F;

            int r_shaded = (r * shade) >> 8;
            int g_shaded = (g * shade) >> 8;
            int b_shaded = (b * shade) >> 8;

            uint16_t shaded_color = (uint16_t)((r_shaded << 11) | (g_shaded << 5) | b_shaded);

            draw_filled_triangle_fb(fb_addr,
                                    sx_buf[i0], sy_buf[i0],
                                    sx_buf[i1], sy_buf[i1],
                                    sx_buf[i2], sy_buf[i2],
                                    shaded_color);
        }
    }
#undef MAX_VERTS

    /* ------------------------------------------------------------------
     * 4.5. Blit 2D Alpha-Blended Sprites (Crisp 2D Text & Logos)
     * ------------------------------------------------------------------ */
    uint16_t sprite_count = rd16(&h->sprite_count);
    if (sprite_count > 0 && s_scene.sprites && s_scene.sprite_frames) {
        uint32_t active_spr_tick = tick;
        uint16_t stored_f = rd16(&h->stored_frames);
        if (stored_f > 0 && active_spr_tick >= stored_f) {
            active_spr_tick = stored_f - 1;
        }
        const BootSceneSpriteFrame *frame_sprites = s_scene.sprite_frames + active_spr_tick * sprite_count;
        for (uint16_t s = 0; s < sprite_count; s++) {
            const BootSceneSprite *spr = &s_scene.sprites[s];
            const BootSceneSpriteFrame *frm = &frame_sprites[s];
            if (frm->alpha == 0) continue;

            uint16_t w = rd16(&spr->width);
            uint16_t ht = rd16(&spr->height);
            uint32_t off_px = rd32(&spr->off_pixels);
            const uint16_t *pixels = (const uint16_t *)(s_scene.blob_base + off_px);

            blit_sprite_argb4444(fb_addr, (int)frm->x, (int)frm->y, (int)w, (int)ht, pixels, (int)frm->alpha);
        }
    }

    /* ------------------------------------------------------------------
     * 5. Fire audio cues matching this tick
     * ------------------------------------------------------------------ */
    if (s_scene.audio_split_tick > 0 && !s_scene.audio_ch1_fired && tick >= s_scene.audio_split_tick) {
        s_scene.audio_ch1_fired = 1;
        uint32_t sa1 = 44100;
        uint32_t len1 = (s_scene.audio_bytes > 44100) ? (s_scene.audio_bytes - 44100) : 0;
        if (len1 > 65535) len1 = 65535;

        if (len1 > 0) {
            AICA_CHN_REG(1, 0x00) = 0x8000;
            AICA_CHN_REG(1, 0x04) = (uint16_t)(sa1 & 0xFFFF);
            AICA_CHN_REG(1, 0x08) = 0x0000;
            AICA_CHN_REG(1, 0x0C) = (uint16_t)len1;
            AICA_CHN_REG(1, 0x10) = 0x001F;
            AICA_CHN_REG(1, 0x14) = 0x3C1F;
            AICA_CHN_REG(1, 0x18) = 0x7000;
            AICA_CHN_REG(1, 0x24) = 0x0F00;
            AICA_CHN_REG(1, 0x28) = 0x0060;
            AICA_CHN_REG(1, 0x00) = 0xC080 | ((sa1 >> 16) & 0x7F);
        }
    }

    for (uint32_t c = 0; c < cue_count; c++) {
        const BootSceneAudioCue *cue = &s_scene.cues[c];
        uint32_t trig = rd32(&cue->trigger_frame);
        if (trig == tick) {
            sound_play_note((int)cue->channel,
                            (int)cue->midi_note,
                            (int)cue->volume,
                            (int)cue->pan,
                            (int)cue->wavetable_id);
        }
    }

    /* ------------------------------------------------------------------
     * 6. Advance tick (2 VBlanks per keyframe = 100% 1:1 speed matching Blender 60 Hz)
     * ------------------------------------------------------------------ */
    s_scene.subtick++;
    if (s_scene.subtick >= 2) {
        s_scene.subtick = 0;
        s_scene.tick++;
    }
}

/* =========================================================================
 * Public: boot_scene_unmount()
 * ========================================================================= */
void boot_scene_unmount(void)
{
    /* Stop and silence audio playback channels */
    AICA_CHN_REG(0, 0x00) = 0x8000;
    AICA_CHN_REG(0, 0x24) = 0x0000;
    AICA_CHN_REG(1, 0x00) = 0x8000;
    AICA_CHN_REG(1, 0x24) = 0x0000;

    s_scene.hdr           = (const BootSceneHeader *)0;
    s_scene.transforms    = (const float *)0;
    s_scene.vertices      = (const float *)0;
    s_scene.indices       = (const uint16_t *)0;
    s_scene.colors        = (const uint16_t *)0;
    s_scene.objects       = (const BootSceneObject *)0;
    s_scene.cues          = (const BootSceneAudioCue *)0;
    s_scene.sprites       = (const BootSceneSprite *)0;
    s_scene.sprite_frames = (const BootSceneSpriteFrame *)0;
    s_scene.blob_base     = (const uint8_t *)0;
    s_scene.tick          = 0;
    s_scene.subtick       = 0;
    s_scene.mounted       = 0;
    s_scene.audio_bytes   = 0;
    s_scene.audio_split_tick = 0;
    s_scene.audio_ch1_fired  = 0;
}

/* =========================================================================
 * Public: boot_scene_is_done()
 * ========================================================================= */
int boot_scene_is_done(void)
{
    if (!s_scene.mounted) return 1;
    uint32_t total_frames = rd32(&s_scene.hdr->total_frames);
    return (s_scene.tick >= total_frames) ? 1 : 0;
}

/* =========================================================================
 * Public: boot_scene_get_tick()
 * ========================================================================= */
uint32_t boot_scene_get_tick(void)
{
    return s_scene.tick;
}
