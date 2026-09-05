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
 * v_cam[3] — camera-space vertex in 16.16
 * sx, sy   — output screen X/Y (integer pixels, 0-origin)
 * Returns 0 if behind camera (Z_cam < 0), 1 if visible.
 * ========================================================================= */
static int project_vertex(const int32_t v_cam[3], int *sx, int *sy)
{
    /* Camera looks along -Z; negate Z_cam for depth test */
    int32_t z = -(v_cam[2] >> 16);   /* integer depth in world units */
    if (z <= 0) return 0;             /* behind or on camera plane    */

    /*
     * sx = cx + focal * x_cam / z_cam
     * We have x_cam and y_cam in 16.16; divide by z to get aspect-correct
     * screen x/y in fraction of "world unit per focal length".
     */
    int32_t x_cam = v_cam[0] >> 16;  /* integer world units */
    int32_t y_cam = v_cam[1] >> 16;

    if (z == 0) z = 1;

    /* Use sdiv32 from math_fx.h (no hardware divider assumption) */
    int screen_x = 320 + sdiv32(x_cam * BOOT_SCENE_FOCAL_LEN, z);
    int screen_y = 240 - sdiv32(y_cam * BOOT_SCENE_FOCAL_LEN, z); /* flip Y */

    *sx = screen_x;
    *sy = screen_y;
    return 1;
}

/* =========================================================================
 * Internal: Flat-shaded wireframe triangle rasterizer
 *
 * Draws 3 lines forming the triangle edges.  The calling convention keeps
 * this compatible with video_draw_line() from video.h.
 * ========================================================================= */
static void draw_triangle_fb(int x0, int y0, int x1, int y1,
                              int x2, int y2, uint16_t color)
{
    video_draw_line(x0, y0, x1, y1, color);
    video_draw_line(x1, y1, x2, y2, color);
    video_draw_line(x2, y2, x0, y0, color);
}

/* =========================================================================
 * Internal: Back-face cull via 2D cross product sign
 *
 * Returns 1 (visible, CCW in screen-space) or 0 (back-face, CW).
 * ========================================================================= */
static int is_front_face(int x0, int y0, int x1, int y1, int x2, int y2)
{
    /* 2D cross product: (v1-v0) × (v2-v0) */
    int32_t cross = (int32_t)(x1 - x0) * (int32_t)(y2 - y0)
                  - (int32_t)(y1 - y0) * (int32_t)(x2 - x0);
    return cross > 0;
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
 * Public: boot_scene_mount()
 * ========================================================================= */
int boot_scene_mount(const void *blob)
{
    if (!blob) return -1;

    const uint8_t *base = (const uint8_t *)blob;

    /* Validate magic and version */
    const BootSceneHeader *h = (const BootSceneHeader *)base;
    if (rd32(&h->magic)   != BOOT_SCENE_MAGIC)   return -1;
    if (h->version        != BOOT_SCENE_VERSION)  return -1;

    /* Sanity: audio_cue_count must not exceed our maximum */
    uint32_t cue_count = rd32(&h->audio_cue_count);
    if (cue_count > BOOT_SCENE_MAX_CUES) return -1;

    /* Map all sub-pointers (zero-copy casts) */
    s_scene.hdr        = h;
    s_scene.transforms = (const float *)(base + rd32(&h->off_transforms));
    s_scene.vertices   = (const float *)(base + rd32(&h->off_vertices));
    s_scene.indices    = (const uint16_t *)(base + rd32(&h->off_indices));
    s_scene.cues       = (const BootSceneAudioCue *)(base + rd32(&h->off_audio_cues));
    s_scene.tick       = 0;
    s_scene.mounted    = 1;

    /* Upload 4 wavetables to AICA SPU RAM at 0xA0800000 */
    const int16_t *wav_block = (const int16_t *)(base + rd32(&h->off_wavetables));
    for (int wt = 0; wt < BOOT_SCENE_WAVETABLE_COUNT; wt++) {
        upload_wavetable((uint32_t)(wt * BOOT_SCENE_WAVETABLE_BYTES),
                         wav_block + wt * BOOT_SCENE_WAVETABLE_SAMPLES);
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
    uint32_t vertex_count  = rd32(&h->vertex_count);
    uint32_t index_count   = rd32(&h->index_count);
    uint32_t cue_count     = rd32(&h->audio_cue_count);

    /* ------------------------------------------------------------------
     * 1. Read per-frame transform (12 floats)
     * ------------------------------------------------------------------ */
    const float *tf = s_scene.transforms + tick * BOOT_SCENE_TRANSFORM_FLOATS;

    float mpos_x = tf[0],  mpos_y = tf[1],  mpos_z = tf[2];
    float mrot_x = tf[3],  mrot_y = tf[4],  mrot_z = tf[5];
    float cpos_x = tf[6],  cpos_y = tf[7],  cpos_z = tf[8];
    float crot_x = tf[9],  crot_y = tf[10], crot_z = tf[11];

    /* ------------------------------------------------------------------
     * 2. Build rotation matrices (integer angles in 256-unit circle)
     * ------------------------------------------------------------------ */
    int32_t m_rot[9], c_rot[9];
    build_rot3x3(deg_to_angle(mrot_x), deg_to_angle(mrot_y), deg_to_angle(mrot_z), m_rot);
    build_rot3x3(deg_to_angle(crot_x), deg_to_angle(crot_y), deg_to_angle(crot_z), c_rot);

    /* Convert positions to 16.16 */
    int32_t m_pos16[3], c_pos16[3];
    m_pos16[0] = (int32_t)(mpos_x * FX16); m_pos16[1] = (int32_t)(mpos_y * FX16); m_pos16[2] = (int32_t)(mpos_z * FX16);
    c_pos16[0] = (int32_t)(cpos_x * FX16); c_pos16[1] = (int32_t)(cpos_y * FX16); c_pos16[2] = (int32_t)(cpos_z * FX16);

    /* ------------------------------------------------------------------
     * 3. Project all vertices → screen-space
     *    We use a small fixed-size scratch buffer on the stack.
     *    Cap at 512 verts max to bound stack usage (512 × 8 bytes = 4 KB).
     * ------------------------------------------------------------------ */
#define MAX_VERTS 512
    int sx_buf[MAX_VERTS], sy_buf[MAX_VERTS];
    uint8_t vis_buf[MAX_VERTS];   /* 1 = visible, 0 = clipped */

    uint32_t nv = (vertex_count < MAX_VERTS) ? vertex_count : MAX_VERTS;

    for (uint32_t i = 0; i < nv; i++) {
        const float *v = s_scene.vertices + i * 3;

        /* Convert vertex to 16.16 */
        int32_t vert16[3];
        vert16[0] = (int32_t)(v[0] * FX16);
        vert16[1] = (int32_t)(v[1] * FX16);
        vert16[2] = (int32_t)(v[2] * FX16);

        /* Apply model rotation + translation → world space (16.16) */
        int32_t world[3];
#define MUL16(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 16))
        world[0] = MUL16(m_rot[0], vert16[0]) + MUL16(m_rot[1], vert16[1]) + MUL16(m_rot[2], vert16[2]) + m_pos16[0];
        world[1] = MUL16(m_rot[3], vert16[0]) + MUL16(m_rot[4], vert16[1]) + MUL16(m_rot[5], vert16[2]) + m_pos16[1];
        world[2] = MUL16(m_rot[6], vert16[0]) + MUL16(m_rot[7], vert16[1]) + MUL16(m_rot[8], vert16[2]) + m_pos16[2];
#undef MUL16

        /* Transform into camera space: cam_space = c_rot^T * (world - c_pos) */
        int32_t cam[3];
        transform_vertex(c_pos16, c_rot, world, cam);

        /* Perspective project → screen pixels */
        vis_buf[i] = (uint8_t)project_vertex(cam, &sx_buf[i], &sy_buf[i]);
    }

    /* ------------------------------------------------------------------
     * 4. Rasterize triangles
     * ------------------------------------------------------------------ */
    uint32_t tri_count = index_count / 3;
    uint16_t mesh_color = RGB565(90, 210, 255);   /* Ice-cyan wireframe */

    for (uint32_t t = 0; t < tri_count; t++) {
        uint32_t base_idx = t * 3;
        uint16_t i0 = s_scene.indices[base_idx + 0];
        uint16_t i1 = s_scene.indices[base_idx + 1];
        uint16_t i2 = s_scene.indices[base_idx + 2];

        /* Bounds guard */
        if (i0 >= nv || i1 >= nv || i2 >= nv) continue;

        /* Skip if any vertex behind camera */
        if (!vis_buf[i0] || !vis_buf[i1] || !vis_buf[i2]) continue;

        /* Back-face cull */
        if (!is_front_face(sx_buf[i0], sy_buf[i0],
                           sx_buf[i1], sy_buf[i1],
                           sx_buf[i2], sy_buf[i2])) continue;

        draw_triangle_fb(sx_buf[i0], sy_buf[i0],
                         sx_buf[i1], sy_buf[i1],
                         sx_buf[i2], sy_buf[i2],
                         mesh_color);
    }
#undef MAX_VERTS

    /* ------------------------------------------------------------------
     * 5. Fire audio cues matching this tick
     * ------------------------------------------------------------------ */
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
     * 6. Advance tick
     * ------------------------------------------------------------------ */
    s_scene.tick++;
}

/* =========================================================================
 * Public: boot_scene_unmount()
 * ========================================================================= */
void boot_scene_unmount(void)
{
    s_scene.hdr        = (const BootSceneHeader *)0;
    s_scene.transforms = (const float *)0;
    s_scene.vertices   = (const float *)0;
    s_scene.indices    = (const uint16_t *)0;
    s_scene.cues       = (const BootSceneAudioCue *)0;
    s_scene.tick       = 0;
    s_scene.mounted    = 0;
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
