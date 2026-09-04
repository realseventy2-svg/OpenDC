#include "theme.h"
#include <kos.h>

#define PACK_ARGB(a, r, g, b) ((((uint32_t)((a)*255.0f)) << 24) | \
                               (((uint32_t)((r)*255.0f)) << 16) | \
                               (((uint32_t)((g)*255.0f)) << 8)  | \
                                ((uint32_t)((b)*255.0f)))

static const theme_t THEMES[] = {
    /* =========================================================================
       1. FRUTIGER AERO: Luminous Sky, Tropical Waters, Floating Glass Bubbles
       ========================================================================= */
    {
        .id = "frutiger_aero",
        .name = "Frutiger Aero",
        .tagline = "Sky & Tropical Liquid Glass",
        .author = "OpenDC Team",

        .bg_clear_r = 0.05f, .bg_clear_g = 0.32f, .bg_clear_b = 0.65f,
        .sky_top    = PACK_ARGB(1.0f, 0.05f, 0.32f, 0.65f),
        .sky_mid    = PACK_ARGB(1.0f, 0.08f, 0.68f, 0.92f),
        .sky_bot    = PACK_ARGB(1.0f, 0.35f, 0.88f, 0.95f),
        .sun_core   = PACK_ARGB(0.35f, 1.0f, 1.0f, 1.0f),
        .sun_fade   = PACK_ARGB(0.00f, 0.6f, 0.9f, 1.0f),

        .aurora_top = PACK_ARGB(0.30f, 0.30f, 1.00f, 0.75f),
        .aurora_bot = PACK_ARGB(0.00f, 0.00f, 0.70f, 1.00f),
        .wave_speed = 1.0f,
        .wave_amplitude = 1.0f,

        .panel_base_top   = PACK_ARGB(0.40f, 0.08f, 0.40f, 0.70f),
        .panel_base_bot   = PACK_ARGB(0.55f, 0.02f, 0.25f, 0.55f),
        .panel_glare_top  = PACK_ARGB(0.55f, 1.00f, 1.00f, 1.00f),
        .panel_glare_bot  = PACK_ARGB(0.08f, 0.85f, 0.98f, 1.00f),
        .panel_border_top = PACK_ARGB(0.70f, 0.75f, 0.95f, 1.00f),
        .panel_border_bot = PACK_ARGB(0.35f, 0.10f, 0.50f, 0.80f),
        .glow_unselected  = PACK_ARGB(0.15f, 0.00f, 0.20f, 0.50f),
        .glow_selected    = PACK_ARGB(0.55f, 0.00f, 0.95f, 0.90f),
        .border_selected  = PACK_ARGB(0.95f, 0.90f, 1.00f, 1.00f),

        .gem_a = PACK_ARGB(0.95f, 0.20f, 0.95f, 0.40f), /* Green */
        .gem_b = PACK_ARGB(0.95f, 0.95f, 0.20f, 0.30f), /* Red */
        .gem_x = PACK_ARGB(0.95f, 0.20f, 0.60f, 0.98f), /* Blue */
        .gem_y = PACK_ARGB(0.95f, 0.98f, 0.85f, 0.10f), /* Yellow */
        .gem_selected = PACK_ARGB(0.95f, 0.20f, 0.95f, 0.40f),

        .swirl_core_a = PACK_ARGB(0.75f, 0.20f, 0.85f, 1.00f),
        .swirl_core_b = PACK_ARGB(0.75f, 0.80f, 0.35f, 0.95f),
        .swirl_glint  = PACK_ARGB(0.95f, 1.00f, 1.00f, 1.00f),
        .disc_body    = PACK_ARGB(0.70f, 0.45f, 0.75f, 0.95f),
        .disc_edge    = PACK_ARGB(0.85f, 0.85f, 0.95f, 1.00f),

        .text_title        = PACK_ARGB(1.00f, 1.00f, 1.00f, 1.00f),
        .text_title_shadow = PACK_ARGB(0.80f, 0.02f, 0.25f, 0.50f),
        .text_body         = PACK_ARGB(0.92f, 0.82f, 0.98f, 1.00f),
        .text_sub          = PACK_ARGB(0.85f, 0.70f, 0.95f, 1.00f),
        .text_dim          = PACK_ARGB(0.60f, 0.68f, 0.82f, 0.95f),
        .text_accent       = PACK_ARGB(1.00f, 0.30f, 1.00f, 0.70f),

        .particle_type       = PARTICLE_BUBBLES,
        .particle_col        = PACK_ARGB(0.65f, 0.85f, 0.95f, 1.00f),
        .particle_speed_mult = 1.0f,

        .chords = {
            { 27, 34, 55, 62, 77 }, /* Ebmaj9 */
            { 24, 31, 51, 58, 79 }, /* Cm11   */
            { 32, 39, 48, 55, 75 }, /* Abmaj9 */
            { 34, 41, 58, 63, 74 }  /* Bbsus4 */
        }
    },

    /* =========================================================================
       2. Y2K CYBER MILLENNIUM: Titanium Silver, Neon Amber & Cyber Digital FX
       ========================================================================= */
    {
        .id = "y2k_cyber",
        .name = "Y2K Millennium",
        .tagline = "Titanium Silver & Neon Amber",
        .author = "OpenDC Team",

        .bg_clear_r = 0.08f, .bg_clear_g = 0.09f, .bg_clear_b = 0.12f,
        .sky_top    = PACK_ARGB(1.0f, 0.08f, 0.09f, 0.14f),
        .sky_mid    = PACK_ARGB(1.0f, 0.18f, 0.16f, 0.22f),
        .sky_bot    = PACK_ARGB(1.0f, 0.35f, 0.25f, 0.15f),
        .sun_core   = PACK_ARGB(0.40f, 1.0f, 0.75f, 0.3f),
        .sun_fade   = PACK_ARGB(0.00f, 0.9f, 0.45f, 0.1f),

        .aurora_top = PACK_ARGB(0.35f, 1.00f, 0.55f, 0.15f),
        .aurora_bot = PACK_ARGB(0.00f, 0.80f, 0.20f, 0.05f),
        .wave_speed = 1.6f,
        .wave_amplitude = 1.3f,

        .panel_base_top   = PACK_ARGB(0.45f, 0.15f, 0.15f, 0.20f),
        .panel_base_bot   = PACK_ARGB(0.65f, 0.25f, 0.18f, 0.12f),
        .panel_glare_top  = PACK_ARGB(0.60f, 1.00f, 0.90f, 0.80f),
        .panel_glare_bot  = PACK_ARGB(0.10f, 1.00f, 0.60f, 0.20f),
        .panel_border_top = PACK_ARGB(0.85f, 1.00f, 0.75f, 0.40f),
        .panel_border_bot = PACK_ARGB(0.40f, 0.60f, 0.30f, 0.10f),
        .glow_unselected  = PACK_ARGB(0.20f, 0.30f, 0.15f, 0.05f),
        .glow_selected    = PACK_ARGB(0.65f, 1.00f, 0.50f, 0.10f),
        .border_selected  = PACK_ARGB(0.98f, 1.00f, 0.80f, 0.40f),

        .gem_a = PACK_ARGB(0.95f, 1.00f, 0.60f, 0.10f), /* Amber Gold */
        .gem_b = PACK_ARGB(0.95f, 0.95f, 0.25f, 0.25f),
        .gem_x = PACK_ARGB(0.95f, 0.30f, 0.75f, 1.00f),
        .gem_y = PACK_ARGB(0.95f, 1.00f, 0.85f, 0.20f),
        .gem_selected = PACK_ARGB(0.95f, 1.00f, 0.55f, 0.10f),

        .swirl_core_a = PACK_ARGB(0.85f, 1.00f, 0.45f, 0.05f), /* Sega Orange */
        .swirl_core_b = PACK_ARGB(0.85f, 1.00f, 0.75f, 0.20f),
        .swirl_glint  = PACK_ARGB(0.98f, 1.00f, 0.95f, 0.85f),
        .disc_body    = PACK_ARGB(0.70f, 0.75f, 0.65f, 0.55f),
        .disc_edge    = PACK_ARGB(0.90f, 1.00f, 0.85f, 0.60f),

        .text_title        = PACK_ARGB(1.00f, 1.00f, 0.95f, 0.88f),
        .text_title_shadow = PACK_ARGB(0.85f, 0.30f, 0.10f, 0.02f),
        .text_body         = PACK_ARGB(0.95f, 1.00f, 0.85f, 0.70f),
        .text_sub          = PACK_ARGB(0.88f, 0.95f, 0.70f, 0.45f),
        .text_dim          = PACK_ARGB(0.65f, 0.75f, 0.65f, 0.60f),
        .text_accent       = PACK_ARGB(1.00f, 1.00f, 0.65f, 0.20f),

        .particle_type       = PARTICLE_CYBER_BITS,
        .particle_col        = PACK_ARGB(0.75f, 1.00f, 0.70f, 0.30f),
        .particle_speed_mult = 1.4f,

        .chords = {
            { 21, 33, 57, 64, 76 }, /* Am9   */
            { 26, 38, 54, 61, 78 }, /* F#m7  */
            { 24, 36, 52, 59, 76 }, /* Fmaj7 */
            { 22, 34, 50, 57, 74 }  /* Em7   */
        }
    },

    /* =========================================================================
       3. AQUA GLOSS: Deep Oceanic Cobalt, Ice Aqua Glass & Liquid Ripples
       ========================================================================= */
    {
        .id = "aqua_gloss",
        .name = "Aqua Gloss",
        .tagline = "Deep Oceanic Cobalt & Ice Glass",
        .author = "OpenDC Team",

        .bg_clear_r = 0.02f, .bg_clear_g = 0.15f, .bg_clear_b = 0.40f,
        .sky_top    = PACK_ARGB(1.0f, 0.02f, 0.12f, 0.35f),
        .sky_mid    = PACK_ARGB(1.0f, 0.05f, 0.35f, 0.65f),
        .sky_bot    = PACK_ARGB(1.0f, 0.15f, 0.65f, 0.85f),
        .sun_core   = PACK_ARGB(0.40f, 0.85f, 0.98f, 1.0f),
        .sun_fade   = PACK_ARGB(0.00f, 0.20f, 0.60f, 0.95f),

        .aurora_top = PACK_ARGB(0.35f, 0.10f, 0.85f, 0.95f),
        .aurora_bot = PACK_ARGB(0.00f, 0.02f, 0.30f, 0.70f),
        .wave_speed = 0.8f,
        .wave_amplitude = 0.9f,

        .panel_base_top   = PACK_ARGB(0.45f, 0.04f, 0.25f, 0.55f),
        .panel_base_bot   = PACK_ARGB(0.65f, 0.02f, 0.15f, 0.40f),
        .panel_glare_top  = PACK_ARGB(0.65f, 0.90f, 0.98f, 1.00f),
        .panel_glare_bot  = PACK_ARGB(0.12f, 0.40f, 0.85f, 1.00f),
        .panel_border_top = PACK_ARGB(0.85f, 0.80f, 0.95f, 1.00f),
        .panel_border_bot = PACK_ARGB(0.45f, 0.10f, 0.40f, 0.75f),
        .glow_unselected  = PACK_ARGB(0.18f, 0.02f, 0.20f, 0.60f),
        .glow_selected    = PACK_ARGB(0.60f, 0.10f, 0.85f, 1.00f),
        .border_selected  = PACK_ARGB(0.95f, 0.85f, 0.98f, 1.00f),

        .gem_a = PACK_ARGB(0.95f, 0.15f, 0.90f, 0.85f), /* Cyan Gem */
        .gem_b = PACK_ARGB(0.95f, 0.95f, 0.30f, 0.40f),
        .gem_x = PACK_ARGB(0.95f, 0.20f, 0.55f, 1.00f),
        .gem_y = PACK_ARGB(0.95f, 1.00f, 0.90f, 0.30f),
        .gem_selected = PACK_ARGB(0.95f, 0.15f, 0.90f, 0.95f),

        .swirl_core_a = PACK_ARGB(0.80f, 0.10f, 0.70f, 0.95f),
        .swirl_core_b = PACK_ARGB(0.80f, 0.30f, 0.90f, 1.00f),
        .swirl_glint  = PACK_ARGB(0.98f, 1.00f, 1.00f, 1.00f),
        .disc_body    = PACK_ARGB(0.70f, 0.35f, 0.65f, 0.90f),
        .disc_edge    = PACK_ARGB(0.85f, 0.75f, 0.92f, 1.00f),

        .text_title        = PACK_ARGB(1.00f, 1.00f, 1.00f, 1.00f),
        .text_title_shadow = PACK_ARGB(0.80f, 0.01f, 0.15f, 0.40f),
        .text_body         = PACK_ARGB(0.95f, 0.85f, 0.95f, 1.00f),
        .text_sub          = PACK_ARGB(0.85f, 0.65f, 0.88f, 1.00f),
        .text_dim          = PACK_ARGB(0.60f, 0.55f, 0.75f, 0.90f),
        .text_accent       = PACK_ARGB(1.00f, 0.25f, 0.95f, 1.00f),

        .particle_type       = PARTICLE_BUBBLES,
        .particle_col        = PACK_ARGB(0.70f, 0.75f, 0.95f, 1.00f),
        .particle_speed_mult = 0.85f,

        .chords = {
            { 26, 38, 57, 62, 74 }, /* Dmaj9 */
            { 31, 43, 59, 66, 78 }, /* Gmaj9 */
            { 29, 41, 57, 64, 76 }, /* F#m7  */
            { 24, 36, 55, 60, 72 }  /* Cmaj7 */
        }
    },

    /* =========================================================================
       4. NOCTURNE VAPOR: Midnight Violet, Neon Magenta & Cosmic Stardust
       ========================================================================= */
    {
        .id = "nocturne_vapor",
        .name = "Nocturne Vapor",
        .tagline = "Midnight Violet & Neon Magenta",
        .author = "OpenDC Team",

        .bg_clear_r = 0.06f, .bg_clear_g = 0.03f, .bg_clear_b = 0.15f,
        .sky_top    = PACK_ARGB(1.0f, 0.05f, 0.02f, 0.14f),
        .sky_mid    = PACK_ARGB(1.0f, 0.22f, 0.08f, 0.38f),
        .sky_bot    = PACK_ARGB(1.0f, 0.45f, 0.12f, 0.55f),
        .sun_core   = PACK_ARGB(0.40f, 1.00f, 0.40f, 0.85f),
        .sun_fade   = PACK_ARGB(0.00f, 0.60f, 0.10f, 0.70f),

        .aurora_top = PACK_ARGB(0.35f, 0.95f, 0.20f, 0.75f),
        .aurora_bot = PACK_ARGB(0.00f, 0.40f, 0.05f, 0.60f),
        .wave_speed = 1.1f,
        .wave_amplitude = 1.1f,

        .panel_base_top   = PACK_ARGB(0.45f, 0.20f, 0.08f, 0.35f),
        .panel_base_bot   = PACK_ARGB(0.65f, 0.12f, 0.04f, 0.24f),
        .panel_glare_top  = PACK_ARGB(0.60f, 1.00f, 0.80f, 0.95f),
        .panel_glare_bot  = PACK_ARGB(0.10f, 0.90f, 0.25f, 0.80f),
        .panel_border_top = PACK_ARGB(0.80f, 0.95f, 0.60f, 0.95f),
        .panel_border_bot = PACK_ARGB(0.40f, 0.50f, 0.15f, 0.65f),
        .glow_unselected  = PACK_ARGB(0.18f, 0.35f, 0.08f, 0.50f),
        .glow_selected    = PACK_ARGB(0.65f, 0.95f, 0.25f, 0.85f),
        .border_selected  = PACK_ARGB(0.98f, 1.00f, 0.70f, 0.95f),

        .gem_a = PACK_ARGB(0.95f, 0.95f, 0.30f, 0.80f), /* Magenta Gem */
        .gem_b = PACK_ARGB(0.95f, 1.00f, 0.35f, 0.45f),
        .gem_x = PACK_ARGB(0.95f, 0.45f, 0.40f, 1.00f),
        .gem_y = PACK_ARGB(0.95f, 1.00f, 0.80f, 0.30f),
        .gem_selected = PACK_ARGB(0.95f, 0.95f, 0.25f, 0.85f),

        .swirl_core_a = PACK_ARGB(0.85f, 0.95f, 0.20f, 0.70f),
        .swirl_core_b = PACK_ARGB(0.85f, 0.55f, 0.25f, 0.95f),
        .swirl_glint  = PACK_ARGB(0.98f, 1.00f, 0.90f, 1.00f),
        .disc_body    = PACK_ARGB(0.70f, 0.65f, 0.35f, 0.75f),
        .disc_edge    = PACK_ARGB(0.85f, 0.90f, 0.70f, 0.95f),

        .text_title        = PACK_ARGB(1.00f, 1.00f, 0.95f, 1.00f),
        .text_title_shadow = PACK_ARGB(0.80f, 0.25f, 0.02f, 0.35f),
        .text_body         = PACK_ARGB(0.95f, 0.95f, 0.85f, 1.00f),
        .text_sub          = PACK_ARGB(0.88f, 0.88f, 0.65f, 0.95f),
        .text_dim          = PACK_ARGB(0.60f, 0.70f, 0.55f, 0.80f),
        .text_accent       = PACK_ARGB(1.00f, 1.00f, 0.45f, 0.90f),

        .particle_type       = PARTICLE_STARDUST,
        .particle_col        = PACK_ARGB(0.75f, 0.95f, 0.70f, 1.00f),
        .particle_speed_mult = 1.0f,

        .chords = {
            { 26, 38, 53, 60, 72 }, /* Dm9    */
            { 22, 34, 50, 57, 69 }, /* Bbmaj7 */
            { 24, 36, 52, 59, 72 }, /* C9     */
            { 21, 33, 48, 55, 67 }  /* Am7    */
        }
    },

    /* =========================================================================
       5. SOLAR SONIC: Cobalt Blue, Emerald Green & Sunset Gold
       ========================================================================= */
    {
        .id = "solar_sonic",
        .name = "Solar Sonic",
        .tagline = "Cobalt Blue & Sunset Gold",
        .author = "OpenDC Team",

        .bg_clear_r = 0.04f, .bg_clear_g = 0.20f, .bg_clear_b = 0.55f,
        .sky_top    = PACK_ARGB(1.0f, 0.02f, 0.18f, 0.55f),
        .sky_mid    = PACK_ARGB(1.0f, 0.10f, 0.45f, 0.80f),
        .sky_bot    = PACK_ARGB(1.0f, 0.95f, 0.55f, 0.15f),
        .sun_core   = PACK_ARGB(0.45f, 1.00f, 0.95f, 0.50f),
        .sun_fade   = PACK_ARGB(0.00f, 1.00f, 0.50f, 0.10f),

        .aurora_top = PACK_ARGB(0.35f, 0.20f, 0.90f, 0.55f),
        .aurora_bot = PACK_ARGB(0.00f, 0.05f, 0.45f, 0.70f),
        .wave_speed = 1.3f,
        .wave_amplitude = 1.2f,

        .panel_base_top   = PACK_ARGB(0.45f, 0.05f, 0.25f, 0.60f),
        .panel_base_bot   = PACK_ARGB(0.65f, 0.25f, 0.15f, 0.35f),
        .panel_glare_top  = PACK_ARGB(0.65f, 1.00f, 0.95f, 0.85f),
        .panel_glare_bot  = PACK_ARGB(0.12f, 1.00f, 0.70f, 0.25f),
        .panel_border_top = PACK_ARGB(0.85f, 1.00f, 0.85f, 0.45f),
        .panel_border_bot = PACK_ARGB(0.40f, 0.20f, 0.45f, 0.75f),
        .glow_unselected  = PACK_ARGB(0.18f, 0.10f, 0.30f, 0.65f),
        .glow_selected    = PACK_ARGB(0.65f, 1.00f, 0.75f, 0.15f),
        .border_selected  = PACK_ARGB(0.98f, 1.00f, 0.90f, 0.50f),

        .gem_a = PACK_ARGB(0.95f, 1.00f, 0.75f, 0.10f), /* Gold Gem */
        .gem_b = PACK_ARGB(0.95f, 0.95f, 0.25f, 0.25f),
        .gem_x = PACK_ARGB(0.95f, 0.20f, 0.60f, 1.00f),
        .gem_y = PACK_ARGB(0.95f, 0.25f, 0.90f, 0.40f),
        .gem_selected = PACK_ARGB(0.95f, 1.00f, 0.75f, 0.10f),

        .swirl_core_a = PACK_ARGB(0.85f, 0.10f, 0.45f, 0.95f), /* Sonic Blue */
        .swirl_core_b = PACK_ARGB(0.85f, 1.00f, 0.70f, 0.10f), /* Ring Gold */
        .swirl_glint  = PACK_ARGB(0.98f, 1.00f, 0.98f, 0.90f),
        .disc_body    = PACK_ARGB(0.70f, 0.40f, 0.65f, 0.90f),
        .disc_edge    = PACK_ARGB(0.85f, 0.95f, 0.85f, 0.55f),

        .text_title        = PACK_ARGB(1.00f, 1.00f, 1.00f, 1.00f),
        .text_title_shadow = PACK_ARGB(0.80f, 0.05f, 0.15f, 0.45f),
        .text_body         = PACK_ARGB(0.95f, 1.00f, 0.92f, 0.80f),
        .text_sub          = PACK_ARGB(0.85f, 0.85f, 0.90f, 1.00f),
        .text_dim          = PACK_ARGB(0.60f, 0.65f, 0.75f, 0.88f),
        .text_accent       = PACK_ARGB(1.00f, 1.00f, 0.80f, 0.20f),

        .particle_type       = PARTICLE_BUBBLES,
        .particle_col        = PACK_ARGB(0.70f, 1.00f, 0.88f, 0.60f),
        .particle_speed_mult = 1.2f,

        .chords = {
            { 29, 41, 57, 64, 76 }, /* Fmaj9 */
            { 24, 36, 52, 59, 72 }, /* Cmaj9 */
            { 26, 38, 53, 60, 72 }, /* Dm9   */
            { 31, 43, 59, 65, 77 }  /* G9    */
        }
    },

    /* =========================================================================
       6. SAKURA BLOSSOM: Soft Rose Pink, Spring White & Blossom Petals
       ========================================================================= */
    {
        .id = "sakura_blossom",
        .name = "Sakura Blossom",
        .tagline = "Soft Rose Pink & Cherry Glass",
        .author = "OpenDC Team",

        .bg_clear_r = 0.20f, .bg_clear_g = 0.08f, .bg_clear_b = 0.16f,
        .sky_top    = PACK_ARGB(1.0f, 0.22f, 0.08f, 0.18f),
        .sky_mid    = PACK_ARGB(1.0f, 0.55f, 0.22f, 0.38f),
        .sky_bot    = PACK_ARGB(1.0f, 0.95f, 0.65f, 0.75f),
        .sun_core   = PACK_ARGB(0.40f, 1.00f, 0.85f, 0.90f),
        .sun_fade   = PACK_ARGB(0.00f, 0.95f, 0.35f, 0.55f),

        .aurora_top = PACK_ARGB(0.35f, 1.00f, 0.45f, 0.65f),
        .aurora_bot = PACK_ARGB(0.00f, 0.65f, 0.15f, 0.35f),
        .wave_speed = 0.9f,
        .wave_amplitude = 0.9f,

        .panel_base_top   = PACK_ARGB(0.45f, 0.45f, 0.15f, 0.30f),
        .panel_base_bot   = PACK_ARGB(0.65f, 0.35f, 0.08f, 0.22f),
        .panel_glare_top  = PACK_ARGB(0.65f, 1.00f, 0.92f, 0.96f),
        .panel_glare_bot  = PACK_ARGB(0.12f, 1.00f, 0.55f, 0.75f),
        .panel_border_top = PACK_ARGB(0.85f, 1.00f, 0.80f, 0.90f),
        .panel_border_bot = PACK_ARGB(0.40f, 0.70f, 0.25f, 0.45f),
        .glow_unselected  = PACK_ARGB(0.18f, 0.50f, 0.15f, 0.35f),
        .glow_selected    = PACK_ARGB(0.65f, 1.00f, 0.45f, 0.75f),
        .border_selected  = PACK_ARGB(0.98f, 1.00f, 0.85f, 0.95f),

        .gem_a = PACK_ARGB(0.95f, 1.00f, 0.45f, 0.65f), /* Rose Gem */
        .gem_b = PACK_ARGB(0.95f, 0.95f, 0.25f, 0.35f),
        .gem_x = PACK_ARGB(0.95f, 0.50f, 0.65f, 1.00f),
        .gem_y = PACK_ARGB(0.95f, 1.00f, 0.85f, 0.35f),
        .gem_selected = PACK_ARGB(0.95f, 1.00f, 0.45f, 0.65f),

        .swirl_core_a = PACK_ARGB(0.85f, 1.00f, 0.40f, 0.60f),
        .swirl_core_b = PACK_ARGB(0.85f, 1.00f, 0.75f, 0.85f),
        .swirl_glint  = PACK_ARGB(0.98f, 1.00f, 0.95f, 0.98f),
        .disc_body    = PACK_ARGB(0.70f, 0.75f, 0.45f, 0.60f),
        .disc_edge    = PACK_ARGB(0.85f, 0.98f, 0.80f, 0.90f),

        .text_title        = PACK_ARGB(1.00f, 1.00f, 0.95f, 0.98f),
        .text_title_shadow = PACK_ARGB(0.80f, 0.35f, 0.05f, 0.20f),
        .text_body         = PACK_ARGB(0.95f, 1.00f, 0.90f, 0.95f),
        .text_sub          = PACK_ARGB(0.85f, 0.95f, 0.75f, 0.85f),
        .text_dim          = PACK_ARGB(0.60f, 0.75f, 0.55f, 0.68f),
        .text_accent       = PACK_ARGB(1.00f, 1.00f, 0.55f, 0.75f),

        .particle_type       = PARTICLE_PETALS,
        .particle_col        = PACK_ARGB(0.75f, 1.00f, 0.80f, 0.88f),
        .particle_speed_mult = 0.9f,

        .chords = {
            { 29, 41, 57, 60, 72 }, /* Fmaj7 */
            { 26, 38, 53, 57, 69 }, /* Dm7   */
            { 22, 34, 50, 53, 65 }, /* Bbmaj7*/
            { 24, 36, 52, 55, 67 }  /* C     */
        }
    }
};

static int s_current_theme_idx = 0;

void theme_init(void) {
    s_current_theme_idx = 0;
}

int theme_get_count(void) {
    return sizeof(THEMES) / sizeof(THEMES[0]);
}

int theme_get_current_index(void) {
    return s_current_theme_idx;
}

const theme_t *theme_get_current(void) {
    return &THEMES[s_current_theme_idx];
}

const theme_t *theme_get_by_index(int index) {
    int total = theme_get_count();
    if (index < 0 || index >= total) index = 0;
    return &THEMES[index];
}

void theme_set_index(int index) {
    int total = theme_get_count();
    if (index < 0) index = 0;
    if (index >= total) index = total - 1;
    s_current_theme_idx = index;
}

void theme_next(void) {
    int total = theme_get_count();
    s_current_theme_idx = (s_current_theme_idx + 1) % total;
}

void theme_prev(void) {
    int total = theme_get_count();
    s_current_theme_idx = (s_current_theme_idx - 1 + total) % total;
}
