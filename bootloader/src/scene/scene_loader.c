#include "scene_loader.h"
#include "video.h"
#include "sound.h"

int scene_loader_mount(const void *blob, BootSceneState *scene)
{
    if (!blob || !scene) return -1;

    const uint8_t *base = (const uint8_t *)blob;
    const BootSceneHeader *h = (const BootSceneHeader *)base;

    /* Validate magic and version */
    uint32_t magic = rd32(&h->magic);
    if (magic != BOOT_SCENE_MAGIC && magic != BOOT_SCENE_MAGIC_BE) return -1;
    uint16_t ver = rd16(&h->version);
    if (ver < 1 || ver > 3) return -1;

    uint32_t cue_count = rd32(&h->audio_cue_count);
    if (cue_count > BOOT_SCENE_MAX_CUES) return -1;

    scene->hdr           = h;
    scene->transforms    = (const float *)(base + rd32(&h->off_transforms));
    scene->vertices      = (const float *)(base + rd32(&h->off_vertices));
    scene->indices       = (const uint16_t *)(base + rd32(&h->off_indices));

    if (ver >= 2 && rd16(&h->object_count) > 0) {
        scene->objects   = (const BootSceneObject *)(base + rd32(&h->off_colors));
        scene->colors    = (const uint16_t *)0;
    } else {
        scene->objects   = (const BootSceneObject *)0;
        scene->colors    = (rd32(&h->off_colors) != 0) ? (const uint16_t *)(base + rd32(&h->off_colors)) : (const uint16_t *)0;
    }

    scene->cues          = (const BootSceneAudioCue *)(base + rd32(&h->off_audio_cues));
    scene->sprites       = (rd32(&h->off_sprites) != 0) ? (const BootSceneSprite *)(base + rd32(&h->off_sprites)) : (const BootSceneSprite *)0;
    scene->sprite_frames = (rd32(&h->off_sprite_frames) != 0) ? (const BootSceneSpriteFrame *)(base + rd32(&h->off_sprite_frames)) : (const BootSceneSpriteFrame *)0;

    scene->bg_color      = RGB565(210, 213, 217); /* Authentic Sega Grey #D2D5D9 */

    scene->tick          = 0;
    scene->mounted       = 1;
    scene->done          = 0;

    /* Initialize AICA SPU hardware, master volume, and channels */
    sound_init();

    /* Upload embedded PCM boot audio to AICA SPU RAM */
    if (rd32(&h->off_audio) != 0 && rd32(&h->audio_sample_count) > 0) {
        uint32_t audio_bytes = rd32(&h->audio_sample_count);
        const uint8_t *audio_src = base + rd32(&h->off_audio);
        volatile uint32_t *dst = (volatile uint32_t *)BOOT_SCENE_AICA_RAM_BASE;
        const uint32_t *src32 = (const uint32_t *)audio_src;
        uint32_t words = (audio_bytes + 3) >> 2;
        for (uint32_t i = 0; i < words; i++) {
            while ((*(volatile uint32_t *)0xA05F688CUL) & 0x11) ;
            dst[i] = src32[i];
        }
    }

    return 0;
}

void scene_loader_unmount(BootSceneState *scene)
{
    if (!scene) return;
    scene->mounted = 0;
    scene->done    = 1;
    scene->hdr     = (const BootSceneHeader *)0;
}
