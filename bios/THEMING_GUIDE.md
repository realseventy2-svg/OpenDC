# OpenDC Custom BIOS Theming & Customization Guide

Welcome to the **OpenDC Custom BIOS Theme Architecture**. This system was designed from the ground up to allow homebrew developers and Sega Dreamcast modders to create, customize, and distribute custom BIOS themes in just a few minutes with zero hassle.

---

## Architecture Overview

All visual styling, color grading, particle dynamics, and SPU hardware music chords are encapsulated in the `theme_t` structure defined in [`projects/OpenDC/bios/src/theme.h`](file:///d:/Github/Personal/KallistiOS/projects/OpenDC/bios/src/theme.h).

Themes are defined as static C structures in [`projects/OpenDC/bios/src/theme.c`](file:///d:/Github/Personal/KallistiOS/projects/OpenDC/bios/src/theme.c).

---

## Adding a Custom Theme in 3 Steps

### Step 1: Add your Theme Definition to `THEMES[]` in `src/theme.c`

Open [`src/theme.c`](file:///d:/Github/Personal/KallistiOS/projects/OpenDC/bios/src/theme.c) and append your new theme struct:

```c
{
    .id = "my_custom_theme",
    .name = "Neon Cyberpunk",
    .tagline = "Electric Lime & Laser Cyan",
    .author = "YourName",

    /* Background Clear Color & Sky Gradients (ARGB) */
    .bg_clear_r = 0.02f, .bg_clear_g = 0.05f, .bg_clear_b = 0.08f,
    .sky_top    = PACK_ARGB(1.0f, 0.02f, 0.04f, 0.10f),
    .sky_mid    = PACK_ARGB(1.0f, 0.05f, 0.20f, 0.30f),
    .sky_bot    = PACK_ARGB(1.0f, 0.10f, 0.85f, 0.60f),
    .sun_core   = PACK_ARGB(0.40f, 0.40f, 1.00f, 0.80f),
    .sun_fade   = PACK_ARGB(0.00f, 0.05f, 0.50f, 0.40f),

    /* Aurora Ribbon Waves */
    .aurora_top = PACK_ARGB(0.35f, 0.10f, 1.00f, 0.70f),
    .aurora_bot = PACK_ARGB(0.00f, 0.00f, 0.40f, 0.50f),
    .wave_speed = 1.4f,
    .wave_amplitude = 1.2f,

    /* Frosted Glass Panels */
    .panel_base_top   = PACK_ARGB(0.45f, 0.05f, 0.20f, 0.25f),
    .panel_base_bot   = PACK_ARGB(0.65f, 0.02f, 0.10f, 0.18f),
    .panel_glare_top  = PACK_ARGB(0.65f, 0.90f, 1.00f, 0.95f),
    .panel_glare_bot  = PACK_ARGB(0.12f, 0.20f, 0.95f, 0.70f),
    .panel_border_top = PACK_ARGB(0.85f, 0.40f, 1.00f, 0.80f),
    .panel_border_bot = PACK_ARGB(0.40f, 0.10f, 0.50f, 0.40f),
    .glow_unselected  = PACK_ARGB(0.18f, 0.05f, 0.30f, 0.25f),
    .glow_selected    = PACK_ARGB(0.65f, 0.10f, 1.00f, 0.70f),
    .border_selected  = PACK_ARGB(0.98f, 0.60f, 1.00f, 0.90f),

    /* Button Gems */
    .gem_a = PACK_ARGB(0.95f, 0.10f, 0.95f, 0.60f),
    .gem_b = PACK_ARGB(0.95f, 0.95f, 0.25f, 0.35f),
    .gem_x = PACK_ARGB(0.95f, 0.20f, 0.70f, 1.00f),
    .gem_y = PACK_ARGB(0.95f, 1.00f, 0.90f, 0.20f),
    .gem_selected = PACK_ARGB(0.95f, 0.10f, 0.95f, 0.60f),

    /* 3D Mascot Swirl & Holographic Disc */
    .swirl_core_a = PACK_ARGB(0.85f, 0.10f, 0.90f, 0.65f),
    .swirl_core_b = PACK_ARGB(0.85f, 0.20f, 0.60f, 1.00f),
    .swirl_glint  = PACK_ARGB(0.98f, 1.00f, 1.00f, 1.00f),
    .disc_body    = PACK_ARGB(0.70f, 0.25f, 0.70f, 0.60f),
    .disc_edge    = PACK_ARGB(0.85f, 0.60f, 1.00f, 0.85f),

    /* Typography */
    .text_title        = PACK_ARGB(1.00f, 1.00f, 1.00f, 1.00f),
    .text_title_shadow = PACK_ARGB(0.80f, 0.01f, 0.15f, 0.20f),
    .text_body         = PACK_ARGB(0.95f, 0.85f, 1.00f, 0.95f),
    .text_sub          = PACK_ARGB(0.85f, 0.60f, 0.95f, 0.80f),
    .text_dim          = PACK_ARGB(0.60f, 0.50f, 0.70f, 0.65f),
    .text_accent       = PACK_ARGB(1.00f, 0.20f, 1.00f, 0.75f),

    /* Particle FX: PARTICLE_BUBBLES, PARTICLE_CYBER_BITS, PARTICLE_STARDUST, PARTICLE_PETALS */
    .particle_type       = PARTICLE_CYBER_BITS,
    .particle_col        = PACK_ARGB(0.75f, 0.30f, 1.00f, 0.80f),
    .particle_speed_mult = 1.3f,

    /* 4-Step AICA Hardware Synthesizer Chord Progression */
    .chords = {
        { 24, 36, 52, 59, 72 }, /* Chord 1: Cmaj7 */
        { 21, 33, 48, 55, 67 }, /* Chord 2: Am7   */
        { 29, 41, 57, 64, 76 }, /* Chord 3: Fmaj9 */
        { 31, 43, 59, 65, 77 }  /* Chord 4: G9    */
    }
}
```

### Step 2: Tuning AICA Ambient Synthesizer Chords
The AICA sound synthesizer loops across 4 harmonic chord steps (every 5 seconds, 20 seconds total).
Each step takes 5 MIDI note numbers:
- `sub_note`: Deep 808-style sine sub bass (MIDI 21 to 45).
- `drone_note`: Resonant middle pad octave (MIDI 30 to 50).
- `pad_left_note`: Harmonic pad spread across the left stereo field (MIDI 48 to 72).
- `pad_right_note`: Harmonic pad spread across the right stereo field (MIDI 48 to 72).
- `chime_note`: Mellow crystal bell / water chime echo melody (MIDI 65 to 88).

### Step 3: Compile and Test
Run the standard WSL build script:
```bash
wsl -d Ubuntu-26.04 -e /mnt/d/Github/Personal/KallistiOS/scripts/kos-exec.sh /mnt/d/Github/Personal/KallistiOS/projects/OpenDC make clean check
```
In the BIOS **System Settings** menu, navigate to **Firmware Theme** and press **Left / Right / (A)** to preview your theme in real-time!
