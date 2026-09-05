#include "boot_scene.h"
#include "scene_types.h"
#include "scene_math.h"
#include "scene_loader.h"
#include "rasterizer.h"
#include "postprocess.h"
#include "sprite.h"
#include "video.h"
#include "sound.h"

/* Global Scene State */
static BootSceneState s_scene;

/* High-speed SDRAM scratchpad arrays at 0x8C100000 (preserves resident STATE area) */
#define MAX_VERTS 4096

static int * const sx_buf       = (int *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x00000);
static int * const sy_buf       = (int *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x04000);
static float * const cx_buf     = (float *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x08000);
static float * const cy_buf     = (float *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x0C000);
static float * const cz_buf     = (float *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x10000);
static uint8_t * const vis_buf  = (uint8_t *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x14000);
static uint16_t * const col_buf = (uint16_t *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x18000);

/* Helper: Copy wavetable block to AICA SPU RAM */
static void upload_wavetable(uint32_t dst_spu_offset, const int16_t *src)
{
    volatile uint32_t *dst =
        (volatile uint32_t *)(BOOT_SCENE_AICA_RAM_BASE + dst_spu_offset);

    const uint16_t *s16 = (const uint16_t *)src;
    for (int i = 0; i < 128; i++) {
        uint32_t lo = (uint32_t)s16[i * 2];
        uint32_t hi = (uint32_t)s16[i * 2 + 1];
        dst[i] = lo | (hi << 16);
    }
}

/* =========================================================================
 * Public API Implementation
 * ========================================================================= */

uint16_t boot_scene_get_bg_color(void)
{
    if (s_scene.mounted) {
        return s_scene.bg_color;
    }
    return RGB565(210, 213, 217); /* Authentic Sega Frosted Grey #D2D5D9 */
}

int boot_scene_mount(const void *blob)
{
    return scene_loader_mount(blob, &s_scene);
}

void boot_scene_unmount(void)
{
    scene_loader_unmount(&s_scene);
}

int boot_scene_is_done(void)
{
    return (!s_scene.mounted || s_scene.done);
}

void boot_scene_tick(uint32_t fb_addr)
{
    if (!s_scene.mounted || s_scene.done) return;

    const BootSceneHeader *h = s_scene.hdr;
    uint32_t total_frames = rd32(&h->total_frames);
    uint32_t tick = s_scene.tick;

    if (tick >= total_frames) {
        s_scene.done = 1;
        return;
    }

    /* 1. Fire Audio (Direct Boot Chime or Audio Cues) */
    uint32_t cue_count = rd32(&h->audio_cue_count);
    if (cue_count == 0 && rd32(&h->off_audio) != 0 && rd32(&h->audio_sample_count) > 0) {
        uint32_t total_samples = rd32(&h->audio_sample_count);
        if (tick == 0) {
            uint32_t seg1_len = (total_samples > 44100) ? 44100 : total_samples;
            sound_play_cue(0, BOOT_SCENE_AICA_RAM_BASE, seg1_len, 11025, 255, 128, 0);
        } else if (tick == 240 && total_samples > 44100) {
            uint32_t seg2_len = total_samples - 44100;
            if (seg2_len > 65535) seg2_len = 65535;
            sound_play_cue(1, BOOT_SCENE_AICA_RAM_BASE + 44100, seg2_len, 11025, 255, 128, 0);
        }
    }

    for (uint32_t c = 0; c < cue_count; c++) {
        const BootSceneAudioCue *cue = &s_scene.cues[c];
        if (rd16(&cue->frame) == (uint16_t)tick) {
            uint32_t spu_addr = rd32(&cue->spu_addr);
            uint32_t sample_count = rd32(&cue->sample_count);
            uint16_t pitch = rd16(&cue->pitch_freq);
            uint8_t chan = cue->channel;
            uint8_t vol = cue->vol;
            uint8_t pan = cue->pan;
            uint8_t loop = cue->loop;

            if (spu_addr == 0 && rd32(&h->off_audio) != 0 && sample_count > 0) {
                const int16_t *audio_src = (const int16_t *)((const uint8_t *)h + rd32(&h->off_audio));
                upload_wavetable(0, audio_src);
                spu_addr = BOOT_SCENE_AICA_RAM_BASE;
            }

            if (sample_count > 0 && pitch > 0) {
                sound_play_cue(chan, spu_addr, sample_count, pitch, vol, pan, loop);
            }
        }
    }

    uint16_t ver = rd16(&h->version);
    uint16_t obj_count = rd16(&h->object_count);

    int bb_min_x = 640, bb_min_y = 480, bb_max_x = -1, bb_max_y = -1;

    if (ver >= 2 && obj_count > 0) {
        /* ==============================================================
         * Version 2/3: Multi-Object / Camera Matrix Render Path
         * ============================================================== */
        uint16_t stored_frames = rd16(&h->sprite_frame_count);
        uint32_t f_idx = tick >> 1;
        if (stored_frames > 0) {
            if (f_idx >= stored_frames) f_idx = stored_frames - 1;
        }

        const int16_t *tf_frame_i16 = (const int16_t *)s_scene.transforms + f_idx * (obj_count * 12);
        const float   *tf_frame_f   = s_scene.transforms + f_idx * (obj_count * 12);

        for (uint16_t o = 0; o < obj_count; o++) {
            const BootSceneObject *obj = &s_scene.objects[o];
            uint32_t total_obj_tris = rd32(&obj->tri_count);
            uint16_t flags = rd16(&obj->flags);

            const int16_t *tf = (ver == 3) ? (tf_frame_i16 + o * 12) : (const int16_t *)0;

            /* Check if object is hidden (all zero transform) */
            if (ver == 3 && tf[0] == 0 && tf[1] == 0 && tf[3] == 0 && tf[4] == 0) {
                continue;
            }

            uint32_t visible_tris = total_obj_tris;
            float m0, m1, m2, m3;
            float m4, m5, m6, m7;
            float m8, m9, m10, m11;

            if (ver == 3) {
                if (flags & 1) {
                    /* Progressive reveal: tf[2] contains baked progressive visible triangle count */
                    visible_tris = (uint16_t)tf[2];
                    if (visible_tris > total_obj_tris) visible_tris = total_obj_tris;
                    m2 = 0.0f;
                } else {
                    m2 = (float)tf[2] * (1.0f / 8192.0f);
                }

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
                const float *tff = tf_frame_f + o * 12;
                m0 = tff[0]; m1 = tff[1]; m2 = tff[2]; m3 = tff[3];
                m4 = tff[4]; m5 = tff[5]; m6 = tff[6]; m7 = tff[7];
                m8 = tff[8]; m9 = tff[9]; m10 = tff[10]; m11 = tff[11];
            }

            if (visible_tris == 0) continue;

            uint32_t sv = rd32(&obj->start_vert);
            uint32_t nv = rd32(&obj->vert_count);
            if (nv > MAX_VERTS) nv = MAX_VERTS;

            uint16_t base_color = rd16(&obj->color);

            for (uint32_t i = 0; i < nv; i++) {
                const float *v = s_scene.vertices + (sv + i) * 6;
                float vx = v[0], vy = v[1], vz = v[2];
                float nx = v[3], ny = v[4], nz = v[5];

                float cx = m0 * vx + m1 * vy + m2 * vz + m3;
                float cy = m4 * vx + m5 * vy + m6 * vz + m7;
                float cz = m8 * vx + m9 * vy + m10 * vz + m11;

                cx_buf[i] = cx;
                cy_buf[i] = cy;
                cz_buf[i] = cz;

                float z_depth = -cz;
                if (z_depth <= 0.05f) {
                    vis_buf[i] = 0;
                } else {
                    float inv_z = (float)BOOT_SCENE_FOCAL_LEN / z_depth;
                    sx_buf[i] = 320 + (int)(cx * inv_z);
                    sy_buf[i] = 240 - (int)(cy * inv_z);
                    vis_buf[i] = 1;
                }

                /* Rotate smoothed vertex normal by camera 3x3 matrix */
                float rnx = m0 * nx + m1 * ny + m2 * nz;
                float rny = m4 * nx + m5 * ny + m6 * nz;
                float rnz = m8 * nx + m9 * ny + m10 * nz;
                float rn2 = rnx * rnx + rny * rny + rnz * rnz;

                col_buf[i] = rasterizer_calc_lighting(rnx, rny, rnz, rn2, base_color);
            }

            uint32_t st = rd32(&obj->start_tri);

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
                int y_min = sy_buf[i0]; if (sy_buf[i1] < y_min) y_min = sy_buf[i0]; if (sy_buf[i2] < y_min) y_min = sy_buf[i2];
                int y_max = sy_buf[i0]; if (sy_buf[i1] > y_max) y_max = sy_buf[i1]; if (sy_buf[i2] > y_max) y_max = sy_buf[i2];

                if (x_min < bb_min_x) bb_min_x = x_min;
                if (x_max > bb_max_x) bb_max_x = x_max;
                if (y_min < bb_min_y) bb_min_y = y_min;
                if (y_max > bb_max_y) bb_max_y = y_max;

                rasterizer_draw_triangle_gouraud(fb_addr,
                                                 sx_buf[i0], sy_buf[i0], col_buf[i0],
                                                 sx_buf[i1], sy_buf[i1], col_buf[i1],
                                                 sx_buf[i2], sy_buf[i2], col_buf[i2]);
            }
        }

        /* Universal sub-pixel silhouette edge anti-aliasing */
        if (bb_max_x >= bb_min_x && bb_max_y >= bb_min_y) {
            uint16_t bg_col = boot_scene_get_bg_color();
            postprocess_smooth_edges(fb_addr, bb_min_x - 1, bb_min_y - 1, bb_max_x + 1, bb_max_y + 1, bg_col);
        }
    }

    /* 2D Sprite Blitting (Letters & Branding) */
    uint16_t spr_count = rd16(&h->sprite_count);
    uint16_t stored_frames = rd16(&h->sprite_frame_count);
    if (spr_count > 0 && stored_frames > 0 && s_scene.sprites && s_scene.sprite_frames) {
        const uint8_t *base = (const uint8_t *)h;
        uint32_t f_idx = tick >> 1;
        if (f_idx >= stored_frames) f_idx = stored_frames - 1;

        for (uint16_t s = 0; s < spr_count; s++) {
            const BootSceneSprite *spr = &s_scene.sprites[s];
            const BootSceneSpriteFrame *sf = &s_scene.sprite_frames[f_idx * spr_count + s];

            uint8_t alpha = sf->alpha;
            if (alpha == 0) continue;

            int16_t x = sf->x;
            int16_t y = sf->y;
            uint16_t w = rd16(&spr->width);
            uint16_t h_px = rd16(&spr->height);
            uint32_t off = rd32(&spr->offset);

            const uint16_t *pixels = (const uint16_t *)(base + off);
            sprite_blit_argb4444(fb_addr, x, y, w, h_px, pixels, alpha);
        }
    }

    s_scene.tick++;
}
