/*
 * math_fx.h — Shared SH-4 Fixed-Point Math Utilities
 * ====================================================
 * Bare-metal, no-libm helpers used across boot_anim.c, sound.c,
 * and boot_scene.c.  All functions are static inline so every TU
 * that includes this header gets its own inlined copy — no external
 * symbol, no duplicate-definition linker error.
 *
 * Angle convention: 256 units = full circle (0..255).
 * sin_fx / cos_fx return values in the range [-256 .. +256] (8.0 fixed).
 */

#ifndef OPENDC_MATH_FX_H
#define OPENDC_MATH_FX_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * 8.8 Fixed-Point Sine Quarter-Wave Table
 * 65 entries cover 0..90 degrees (0..64 in the 256-unit circle).
 * Result range: 0..256  (256 == 1.0 in 8.0 fixed-point)
 * ------------------------------------------------------------------------- */
static const int16_t MATH_SIN_QUARTER[65] = {
      0,   6,  12,  18,  25,  31,  37,  43,
     49,  56,  62,  68,  74,  80,  86,  92,
     97, 103, 109, 115, 120, 126, 131, 136,
    142, 147, 152, 157, 162, 167, 171, 176,
    181, 185, 189, 193, 197, 201, 205, 208,
    212, 215, 219, 222, 225, 228, 231, 233,
    236, 238, 240, 242, 244, 246, 247, 249,
    250, 251, 252, 253, 254, 254, 255, 255,
    256
};

/* sin(angle)  — angle in [0..255], result in [-256..+256] */
static inline int32_t sin_fx(int angle)
{
    angle &= 0xFF;
    if (angle <= 64)  return  MATH_SIN_QUARTER[angle];
    if (angle <= 128) return  MATH_SIN_QUARTER[128 - angle];
    if (angle <= 192) return -MATH_SIN_QUARTER[angle - 128];
    return                   -MATH_SIN_QUARTER[256 - angle];
}

/* cos(angle) == sin(angle + 64) */
static inline int32_t cos_fx(int angle)
{
    return sin_fx(angle + 64);
}

/* -------------------------------------------------------------------------
 * Unsigned 32-bit integer division (no hardware divider assumed)
 * Returns 0 when den == 0.
 * ------------------------------------------------------------------------- */
static inline uint32_t udiv32(uint32_t num, uint32_t den)
{
    if (den == 0) return 0;
    uint32_t quot = 0, qbit = 1;
    while ((int32_t)den >= 0 && den < num) {
        den  <<= 1;
        qbit <<= 1;
    }
    while (qbit) {
        if (num >= den) { num -= den; quot |= qbit; }
        den  >>= 1;
        qbit >>= 1;
    }
    return quot;
}

/* Signed 32-bit integer division — preserves sign, returns 0 on div/0 */
static inline int32_t sdiv32(int32_t num, int32_t den)
{
    if (den == 0) return 0;
    int      sign = 1;
    uint32_t unum, uden;
    if (num < 0) { sign = -sign; unum = (uint32_t)-num; } else { unum = (uint32_t)num; }
    if (den < 0) { sign = -sign; uden = (uint32_t)-den; } else { uden = (uint32_t)den; }
    uint32_t q = udiv32(unum, uden);
    return (sign < 0) ? -(int32_t)q : (int32_t)q;
}

/* -------------------------------------------------------------------------
 * Clamp helpers
 * ------------------------------------------------------------------------- */
static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline int16_t clamp16(int32_t v)
{
    return (int16_t)clamp_i32(v, -32768, 32767);
}

#endif /* OPENDC_MATH_FX_H */
