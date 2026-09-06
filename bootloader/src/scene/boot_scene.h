#ifndef OPENDC_BOOT_SCENE_H
#define OPENDC_BOOT_SCENE_H

#include <stdint.h>
#include <stddef.h>
#include "scene_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * Returns the RGB565 background clear color specified in the mounted scene container
 * (or authentic Dreamcast slate-grey #D2D5D9 by default).
 */
uint16_t boot_scene_get_bg_color(void);

/**
 * Validate and mount a boot_scene.bin blob.
 * Returns 0 on success, negative error code on failure.
 */
int boot_scene_mount(const void *blob);

/**
 * Advance one VBlank frame: transforms, rasterizes, blends sprites, and fires audio cues.
 */
void boot_scene_tick(uint32_t fb_addr);

/**
 * Release the mounted scene.
 */
void boot_scene_unmount(void);

/**
 * Returns 1 when animation timeline has completed (or unmounted).
 */
int boot_scene_is_done(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENDC_BOOT_SCENE_H */
