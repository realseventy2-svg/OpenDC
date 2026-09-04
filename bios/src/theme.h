#ifndef THEME_H
#define THEME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Particle FX Types */
typedef enum {
    PARTICLE_BUBBLES = 0,   /* Smooth translucent spherical glass bubbles */
    PARTICLE_CYBER_BITS,    /* Sharp glowing square cyber motes */
    PARTICLE_STARDUST,      /* Twinkling ethereal diamond particles */
    PARTICLE_PETALS         /* Soft floating blossom motes */
} particle_type_t;

/* AICA SPU Harmonic Chord Voice Configuration */
typedef struct {
    uint8_t sub_note;       /* Deep bass note (MIDI 21..108) */
    uint8_t drone_note;     /* Resonant drone note */
    uint8_t pad_left_note;  /* Celestial pad chord left voice */
    uint8_t pad_right_note; /* Celestial pad chord right voice */
    uint8_t chime_note;     /* Soft mellow chime / water drop melody */
} theme_chord_step_t;

/* Complete BIOS Visual & Acoustic Theme Definition */
typedef struct {
    const char *id;             /* Unique identifier string (e.g. "frutiger_aero") */
    const char *name;           /* Human-readable theme name */
    const char *tagline;        /* Short descriptive tagline */
    const char *author;         /* Theme author / modder credit */

    /* Clear Screen & Background Atmosphere */
    float bg_clear_r, bg_clear_g, bg_clear_b;
    uint32_t sky_top;           /* Sky horizon top color (ARGB) */
    uint32_t sky_mid;           /* Sky horizon middle color (ARGB) */
    uint32_t sky_bot;           /* Sky horizon bottom / ocean color (ARGB) */
    uint32_t sun_core;          /* Luminous celestial orb core color */
    uint32_t sun_fade;          /* Luminous celestial orb glow fade */

    /* Undulating Aurora / Horizon Ribbon Waves */
    uint32_t aurora_top;        /* Upper ribbon wave color */
    uint32_t aurora_bot;        /* Lower ribbon wave fade color */
    float wave_speed;           /* Ribbon undulation speed multiplier */
    float wave_amplitude;       /* Ribbon vertical displacement multiplier */

    /* Frosted Glass Morphism Panels */
    uint32_t panel_base_top;    /* Glass panel base fill top gradient */
    uint32_t panel_base_bot;    /* Glass panel base fill bottom gradient */
    uint32_t panel_glare_top;   /* Glass specular reflection top color */
    uint32_t panel_glare_bot;   /* Glass specular reflection bottom color */
    uint32_t panel_border_top;  /* Acrylic top rim highlight */
    uint32_t panel_border_bot;  /* Acrylic bottom rim bevel */
    uint32_t glow_unselected;   /* Subtle ambient backlight */
    uint32_t glow_selected;     /* Luminous active card glow */
    uint32_t border_selected;   /* Highlighted card border rim */

    /* Controller Button Gems & Accents */
    uint32_t gem_a;             /* (A) Button Gem / Confirm Color */
    uint32_t gem_b;             /* (B) Button Gem / Cancel Color */
    uint32_t gem_x;             /* (X) Button Gem / Secondary Color */
    uint32_t gem_y;             /* (Y) Button Gem / Tertiary Color */
    uint32_t gem_selected;      /* Menu card active bullet indicator gem */

    /* 3D Mascot Swirl & Holographic Disc */
    uint32_t swirl_core_a;      /* Swirl jewel core primary color */
    uint32_t swirl_core_b;      /* Swirl jewel core secondary color */
    uint32_t swirl_glint;       /* Swirl facet specular reflection */
    uint32_t disc_body;         /* Holographic GD-ROM substrate tint */
    uint32_t disc_edge;         /* GD-ROM outer rim bevel */

    /* Typography Color Palette */
    uint32_t text_title;        /* Primary headers & titles */
    uint32_t text_title_shadow; /* Primary header drop shadow */
    uint32_t text_body;         /* Card descriptions & body text */
    uint32_t text_sub;          /* Subtitles & metadata */
    uint32_t text_dim;          /* Inactive / unselected text */
    uint32_t text_accent;       /* Live clock, alerts, & badges */

    /* Particle FX Environment */
    particle_type_t particle_type;
    uint32_t particle_col;      /* Tint color for floating particles */
    float particle_speed_mult;  /* Particle vertical drift speed */

    /* AICA SPU Atmospheric Music Progression (4 Chords, 20s Loop) */
    theme_chord_step_t chords[4];
} theme_t;

/* Theme Registry API */
void theme_init(void);
int theme_get_count(void);
int theme_get_current_index(void);
const theme_t *theme_get_current(void);
const theme_t *theme_get_by_index(int index);
void theme_set_index(int index);
void theme_next(void);
void theme_prev(void);

#ifdef __cplusplus
}
#endif

#endif /* THEME_H */
