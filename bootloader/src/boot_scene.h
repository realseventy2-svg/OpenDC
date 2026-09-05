/*
 * boot_scene.h — Plug-and-Play Boot Scene Container Runtime
 * ==========================================================
 * Defines the binary layout for boot_scene.bin and the bare-metal
 * C99 API that mounts, drives, and unmounts the scene at 60 Hz.
 *
 * Container magic: 0x53424344  ("DCBS" little-endian = D C B S)
 * All multi-byte fields in the file are little-endian.
 *
 * Zero dynamic allocation — all pointers are computed as
 * (uint8_t*)blob + offset so the file can live in ROM or RAM.
 *
 * Hardware addresses assumed:
 *   AICA SPU RAM base : 0xA0800000  (uncached)
 *   Framebuffer page 0: 0xA5000000
 *   Framebuffer page 1: 0xA5100000
 */

#ifndef OPENDC_BOOT_SCENE_H
#define OPENDC_BOOT_SCENE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Binary Layout Constants
 * ========================================================================= */

#define BOOT_SCENE_MAGIC        0x53424344UL  /* "DCBS" little-endian */
#define BOOT_SCENE_VERSION      1

#define BOOT_SCENE_MAX_CUES     32
#define BOOT_SCENE_WAVETABLE_SAMPLES  256     /* samples per wavetable */
#define BOOT_SCENE_WAVETABLE_BYTES    512     /* 256 × int16_t */
#define BOOT_SCENE_WAVETABLE_COUNT    4
#define BOOT_SCENE_WAVETABLE_BLOCK    2048    /* 4 × 512 bytes */

/* AICA SPU RAM base (uncached P2 mirror) */
#define BOOT_SCENE_AICA_RAM_BASE      0xA0800000UL

/* Focal length for perspective projection (screen units @ 640×480) */
#define BOOT_SCENE_FOCAL_LEN          256

/* =========================================================================
 * Packed Structures (map directly onto blob bytes)
 * ========================================================================= */

/* 64-byte container header — must be kept exactly 64 bytes */
typedef struct {
    uint32_t magic;           /* 0x53424344  "DCBS"                     */
    uint16_t version;         /* Must equal BOOT_SCENE_VERSION (1)       */
    uint16_t flags;           /* Reserved, must be 0                     */
    uint32_t total_frames;    /* Number of animation ticks (VBlanks)     */
    uint32_t vertex_count;    /* Number of float[3] vertices             */
    uint32_t index_count;     /* Number of uint16_t indices (tri×3)      */
    uint32_t audio_cue_count; /* Entries in AudioCue array (max 32)      */
    uint32_t off_transforms;  /* Byte offset → float[12] × total_frames  */
    uint32_t off_vertices;    /* Byte offset → float[3] × vertex_count   */
    uint32_t off_indices;     /* Byte offset → uint16_t × index_count    */
    uint32_t off_audio_cues;  /* Byte offset → AudioCue × audio_cue_count*/
    uint32_t off_wavetables;  /* Byte offset → 4 × 512-byte wavetables   */
    /*
     * Layout: 4+2+2 + 9×4 + 5×4 = 8 + 36 + 20 = 64 bytes exactly.
     *
     *   0x00  magic           4
     *   0x04  version         2
     *   0x06  flags           2
     *   0x08  total_frames    4
     *   0x0C  vertex_count    4
     *   0x10  index_count     4
     *   0x14  audio_cue_count 4
     *   0x18  off_transforms  4
     *   0x1C  off_vertices    4
     *   0x20  off_indices     4
     *   0x24  off_audio_cues  4
     *   0x28  off_wavetables  4
     *   0x2C  reserved[0..4] 20  (5 × uint32)
     *   ─────────────────────────
     *   Total               64
     */
    uint32_t reserved[5];     /* Pad to exactly 64 bytes                 */
} __attribute__((packed)) BootSceneHeader;

/* Compile-time layout guard */
typedef char _bsh_size_check[ (sizeof(BootSceneHeader) == 64) ? 1 : -1 ];

/*
 * Per-frame transform: 12 floats packed as:
 *   [0..2]  model position  (x, y, z)
 *   [3..5]  model rotation  (rx, ry, rz) degrees
 *   [6..8]  camera position (x, y, z)
 *   [9..11] camera rotation (rx, ry, rz) degrees
 */
#define BOOT_SCENE_TRANSFORM_FLOATS  12

/*
 * Audio cue entry — 16 bytes, 32-bit aligned.
 * Fired on the VBlank tick when tick == trigger_frame.
 */
typedef struct {
    uint32_t trigger_frame;   /* VBlank index to fire this note          */
    uint8_t  channel;         /* AICA hardware channel 0..63             */
    uint8_t  wavetable_id;    /* 0..3 — maps to one of the 4 wavetables  */
    uint8_t  midi_note;       /* MIDI note number 21..108                */
    uint8_t  volume;          /* 0..15 (maps to AICA DISDL attenuation)  */
    uint8_t  pan;             /* 0..31 (AICA DIPAN value)                */
    uint8_t  adsr_preset;     /* 0=Bell 1=Strings 2=Bass 3=Shimmer       */
    uint8_t  reserved[2];     /* Pad to 16 bytes                         */
} __attribute__((packed)) BootSceneAudioCue;

typedef char _bsac_size_check[ (sizeof(BootSceneAudioCue) == 16) ? 1 : -1 ];

/* =========================================================================
 * Runtime State (lives in BSS — no dynamic allocation)
 * ========================================================================= */

typedef struct {
    /* Cached sub-pointers into the mounted blob */
    const BootSceneHeader    *hdr;
    const float              *transforms;   /* float[12] × total_frames  */
    const float              *vertices;     /* float[3]  × vertex_count  */
    const uint16_t           *indices;      /* uint16_t  × index_count   */
    const BootSceneAudioCue  *cues;         /* AudioCue  × cue_count     */

    /* Playback state */
    uint32_t tick;           /* Current VBlank frame index               */
    int      mounted;        /* Non-zero if a scene is currently mounted */
} BootSceneState;

/* =========================================================================
 * Public API
 * ========================================================================= */

/*
 * boot_scene_mount() — Validate and mount a boot_scene.bin blob.
 *
 * blob : pointer to the start of the binary (ROM or RAM, uncached OK).
 *        Must remain valid for the lifetime of the scene.
 *
 * Side effects:
 *   - Validates magic word and version.
 *   - Maps all sub-pointers (zero-copy, no malloc).
 *   - Copies 4 × 512-byte wavetables to AICA SPU RAM at 0xA0800000
 *     via 32-bit uncached writes (suitable for bare-metal pre-AICA-init).
 *   - Resets the internal tick counter to 0.
 *
 * Returns 0 on success, -1 on validation failure.
 */
int boot_scene_mount(const void *blob);

/*
 * boot_scene_tick() — Advance one VBlank frame.
 *
 * fb_addr : physical framebuffer address (e.g. 0xA5000000 or 0xA5100000).
 *           The scene is rendered into this buffer via direct pixel writes.
 *
 * Per-tick actions (in order):
 *   1. Bounds-check: no-op if tick >= total_frames or not mounted.
 *   2. Read float[12] transform for this tick.
 *   3. Build integer model-view matrix (fixed-point 16.16) from pos/rot.
 *   4. Project each vertex via perspective divide (focal = 256).
 *   5. Iterate triangles: back-face cull, flat-shade, draw to framebuffer.
 *   6. Scan AudioCue array; call sound_play_note() for matching triggers.
 *   7. Increment internal tick counter.
 *
 * NOTE on PVR TA path:
 *   For a full PowerVR Tile Accelerator display-list submission path the
 *   caller would need to initialise the TA (PARAM_BASE, REGION_BASE, etc.)
 *   before calling boot_scene_tick() and submit a START_RENDER command
 *   after it.  The current implementation writes directly to the
 *   framebuffer (matching the existing boot_anim.c pipeline) so no
 *   additional TA setup is required.  A TA-path variant can be added by
 *   replacing draw_triangle_fb() with TA strip-command emission.
 */
void boot_scene_tick(uint32_t fb_addr);

/*
 * boot_scene_unmount() — Release the mounted scene.
 * Clears all cached pointers and resets the tick counter.
 * Does NOT silence AICA channels — call sound_stop() separately.
 */
void boot_scene_unmount(void);

/*
 * boot_scene_is_done() — Returns 1 when tick >= total_frames (or unmounted).
 */
int boot_scene_is_done(void);

/*
 * boot_scene_get_tick() — Returns the current tick index.
 */
uint32_t boot_scene_get_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENDC_BOOT_SCENE_H */
