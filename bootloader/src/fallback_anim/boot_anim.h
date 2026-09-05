#ifndef OPENDC_BOOTLOADER_ANIM_H
#define OPENDC_BOOTLOADER_ANIM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration for 3D Boot Scene */
typedef struct {
    const char *title;          /* Main console title (e.g. "Open Dreamcast") */
    const char *subtitle;       /* Subtitle banner (e.g. "SEGA ARCHITECTURE") */
    uint16_t swirl_color_a;     /* Primary swirl ribbon hue (e.g. Dreamcast Orange) */
    uint16_t swirl_color_b;     /* Secondary gradient hue (e.g. Radiant Gold) */
    uint16_t swirl_glint_color; /* Specular glint color */
    uint16_t bg_color;          /* Screen background clear color */
    int num_particles;          /* Active 3D orbital particle count */
    int hide_2d_logo;           /* 1 = suppress 2D glass logo sprite when 3D scene is active */
} boot_scene_config_t;


/* Initialize 3D camera, vertex buffers, and particle vortex */
void boot_anim_init(const boot_scene_config_t *config);

/* Render a single 3D boot animation frame (frame_idx = 0..total_frames-1) */
void boot_anim_render_frame(int frame, int total_frames, uint32_t fb_addr);

/* Cleanly finalize 3D animation before payload handoff */
void boot_anim_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENDC_BOOTLOADER_ANIM_H */
