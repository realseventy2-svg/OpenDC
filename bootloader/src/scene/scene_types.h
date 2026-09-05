#ifndef OPENDC_SCENE_TYPES_H
#define OPENDC_SCENE_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Constants & Binary Container Magic
 * ========================================================================= */
#define BOOT_SCENE_MAGIC           0x53424344UL   /* 'DCBS' Little-Endian: 'D', 'C', 'B', 'S' */
#define BOOT_SCENE_MAGIC_BE        0x44434253UL   /* 'DCBS' Big-Endian */
#define BOOT_SCENE_MAX_VERTS       4096
#define BOOT_SCENE_MAX_TRIS        4096
#define BOOT_SCENE_MAX_CUES        16
#define BOOT_SCENE_TRANSFORM_FLOATS 12
#define BOOT_SCENE_FOCAL_LEN       700
#define BOOT_SCENE_AICA_RAM_BASE   0xA0800000UL

/* Dedicated high-speed scratchpad buffer pool in SDRAM (outside of resident STATE area) */
#define BOOT_SCENE_SCRATCHPAD_BASE 0x8C100000UL

/* =========================================================================
 * Binary Container On-Disk Layout (64-byte Header)
 * ========================================================================= */

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;               /* 0x44434253 'DCBS'                        */
    uint16_t version;             /* 1 = single mesh, 2 = multi, 3 = fast tf   */
    uint16_t object_count;        /* Number of 3D mesh objects                 */
    uint32_t total_frames;        /* Timeline length in VBlank ticks           */
    uint32_t vertex_count;        /* Global vertex count                       */
    uint32_t index_count;         /* Global index count                        */
    uint32_t audio_cue_count;     /* Number of audio cues                      */
    uint32_t off_transforms;      /* Byte offset: transforms                   */
    uint32_t off_vertices;        /* Byte offset: vertices                     */
    uint32_t off_indices;         /* Byte offset: indices                      */
    uint32_t off_audio_cues;      /* Byte offset: audio cues                   */
    uint32_t off_audio;           /* Byte offset: SPU PCM audio data           */
    uint32_t off_colors;          /* Byte offset: colors / object table        */
    uint32_t off_sprites;         /* Byte offset: sprite definitions           */
    uint16_t sprite_count;        /* Number of 2D sprites                      */
    uint16_t sprite_frame_count;  /* Total frames for sprites                  */
    uint32_t off_sprite_frames;   /* Byte offset: sprite keyframes             */
    uint32_t audio_sample_count;  /* Total SPU PCM audio samples               */
} BootSceneHeader;

typedef struct {
    uint32_t start_vert;          /* Starting vertex index                     */
    uint32_t vert_count;          /* Number of vertices in object              */
    uint32_t start_tri;           /* Starting triangle index                   */
    uint32_t tri_count;           /* Number of triangles                       */
    uint16_t color;               /* Base RGB565 material color                */
    uint16_t flags;               /* Bit 0: progressive reveal                 */
} BootSceneObject;

typedef struct {
    uint16_t width;               /* Sprite native width (pixels)              */
    uint16_t height;              /* Sprite native height (pixels)             */
    uint32_t offset;              /* Byte offset from file start to ARGB4444   */
} BootSceneSprite;

typedef struct {
    int16_t  x;                   /* Screen X position (pixels, 0-origin)      */
    int16_t  y;                   /* Screen Y position (pixels, 0-origin)      */
    uint8_t  alpha;               /* Opacity 0..255                            */
    uint8_t  pad[3];              /* Alignment                                 */
} BootSceneSpriteFrame;

typedef struct {
    uint16_t frame;               /* VBlank tick when audio triggers           */
    uint8_t  channel;             /* SPU Channel (0..63)                       */
    uint8_t  loop;                /* 0 = one-shot, 1 = loop                    */
    uint32_t spu_addr;            /* Byte address in AICA SPU RAM              */
    uint32_t sample_count;        /* Number of 16-bit PCM samples              */
    uint16_t pitch_freq;          /* Playback frequency (Hz)                   */
    uint8_t  vol;                 /* Volume 0..255                             */
    uint8_t  pan;                 /* Panning 0 (left) .. 128 (mid) .. 255 (rt) */
} BootSceneAudioCue;

#pragma pack(pop)

/* =========================================================================
 * Runtime State
 * ========================================================================= */
typedef struct {
    const BootSceneHeader       *hdr;
    const float                 *transforms;
    const float                 *vertices;
    const uint16_t              *indices;
    const BootSceneObject       *objects;
    const uint16_t              *colors;
    const BootSceneAudioCue     *cues;
    const BootSceneSprite       *sprites;
    const BootSceneSpriteFrame  *sprite_frames;
    uint16_t                     bg_color;
    uint32_t                     tick;
    int                          mounted;
    int                          done;
} BootSceneState;

/* Little-endian safe memory readers */
static inline uint16_t rd16(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)(b[0] | (b[1] << 8));
}

static inline uint32_t rd32(const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

#endif /* OPENDC_SCENE_TYPES_H */
