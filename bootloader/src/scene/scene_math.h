#ifndef OPENDC_SCENE_MATH_H
#define OPENDC_SCENE_MATH_H

#include <stdint.h>
#include "scene_types.h"

/* 16.16 Fixed point scaling */
#define FX16            65536
#define MUL16(a, b)     ((int32_t)(((int64_t)(a) * (b)) >> 16))

/* Standalone 32-bit signed division without libgcc __divdi3 */
static inline int32_t sdiv32(int32_t num, int32_t den)
{
    if (den == 0) return 0;
    int sign = 1;
    uint32_t unum, uden;

    if (num < 0) { sign = -sign; unum = (uint32_t)(-num); }
    else         { unum = (uint32_t)num; }

    if (den < 0) { sign = -sign; uden = (uint32_t)(-den); }
    else         { uden = (uint32_t)den; }

    uint32_t quot = 0, rem = 0;
    for (int i = 31; i >= 0; i--) {
        rem = (rem << 1) | ((unum >> i) & 1);
        if (rem >= uden) {
            rem -= uden;
            quot |= (1U << i);
        }
    }
    return (sign < 0) ? -(int32_t)quot : (int32_t)quot;
}

/* 256-entry Sine table (0..255 maps to 0..2*PI, values in 16.16) */
static const int32_t s_sin256[256] = {
        0,   1608,   3215,   4821,   6424,   8022,   9616,  11204,
    12785,  14359,  15924,  17479,  19024,  20557,  22078,  23586,
    25079,  26557,  28020,  29465,  30893,  32302,  33692,  35061,
    36409,  37736,  39039,  40319,  41575,  42806,  44011,  45189,
    46340,  47464,  48558,  49624,  50659,  51664,  52638,  53580,
    54489,  55364,  56206,  57013,  57785,  58521,  59221,  59885,
    60511,  61100,  61650,  62162,  62634,  63067,  63460,  63812,
    64124,  64394,  64623,  64810,  64955,  65058,  65119,  65137,
    65114,  65048,  64940,  64790,  64598,  64364,  64089,  63772,
    63414,  63015,  62575,  62095,  61575,  61015,  60416,  59778,
    59102,  58387,  57635,  56845,  56019,  55157,  54258,  53325,
    52357,  51355,  50320,  49252,  48152,  47021,  45860,  44668,
    43448,  42199,  40923,  39620,  38291,  36937,  35559,  34158,
    32734,  31289,  29823,  28338,  26834,  25313,  23776,  22223,
    20656,  19076,  17483,  15879,  14264,  12640,  11008,   9369,
     7724,   6074,   4421,   2765,   1108,   -551,  -2211,  -3870,
    -5527,  -7181,  -8831, -10476, -12114, -13744, -15364, -16973,
   -18570, -20153, -21721, -23272, -24804, -26317, -27808, -29277,
   -30721, -32140, -33532, -34895, -36228, -37529, -38798, -40032,
   -41230, -42391, -43513, -44595, -45636, -46634, -47589, -48498,
   -49362, -50178, -50947, -51666, -52336, -52955, -53522, -54038,
   -54499, -54907, -55259, -55556, -55796, -55979, -56104, -56172,
   -56181, -56132, -56024, -55857, -55631, -55347, -55004, -54602,
   -54143, -53625, -53050, -52418, -51728, -50983, -50181, -49324,
   -48412, -47446, -46427, -45354, -44230, -43054, -41828, -40552,
   -39228, -37857, -36440, -34978, -33472, -31923, -30334, -28704,
   -27037, -25332, -23592, -21818, -20011, -18174, -16307, -14413,
   -12493, -10549,  -8583,  -6597,  -4592,  -2571,   -536,   1511,
     3567,   5630,   7698,   9768,  11838,  13907,  15971,  18030,
    20080,  22120,  24147,  26159,  28153,  30126,  32077,  34002,
    35899,  37766,  39600,  41399,  43160,  44880,  46558,  48191,
    49776,  51312,  52796,  54226,  55601,  56917,  58174,  59368
};

static inline int32_t sin16(int32_t angle_byte) {
    return s_sin256[(uint8_t)angle_byte];
}

static inline int32_t cos16(int32_t angle_byte) {
    return s_sin256[(uint8_t)(angle_byte + 64)];
}

static inline int32_t deg_to_angle(float deg) {
    float norm = deg * (256.0f / 360.0f);
    int32_t a = (int32_t)norm;
    return a & 0xFF;
}

static inline void build_rot3x3(int32_t rx, int32_t ry, int32_t rz, int32_t rot[9])
{
    int32_t cx = cos16(rx), sx = sin16(rx);
    int32_t cy = cos16(ry), sy = sin16(ry);
    int32_t cz = cos16(rz), sz = sin16(rz);

    rot[0] = MUL16(cy, cz);
    rot[1] = MUL16(MUL16(sx, sy), cz) - MUL16(cx, sz);
    rot[2] = MUL16(MUL16(cx, sy), cz) + MUL16(sx, sz);

    rot[3] = MUL16(cy, sz);
    rot[4] = MUL16(MUL16(sx, sy), sz) + MUL16(cx, cz);
    rot[5] = MUL16(MUL16(cx, sy), sz) - MUL16(sx, cz);

    rot[6] = -sy;
    rot[7] = MUL16(sx, cy);
    rot[8] = MUL16(cx, cy);
}

static inline int is_front_face(int x0, int y0, int x1, int y1, int x2, int y2)
{
    int32_t cross = (int32_t)(x1 - x0) * (int32_t)(y2 - y0)
                  - (int32_t)(y1 - y0) * (int32_t)(x2 - x0);
    return cross < 0;
}

#endif /* OPENDC_SCENE_MATH_H */
