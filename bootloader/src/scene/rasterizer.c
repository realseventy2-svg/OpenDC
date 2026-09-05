#include "rasterizer.h"
#include "scene_math.h"
#include "video.h"

/* Fast Solid Filled Span Filler with unrolled 32-bit burst writes */
void rasterizer_draw_span(uint32_t fb_addr, int y, int x0, int x1, uint16_t color)
{
    if (y < 0 || y >= 480) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= 640) x1 = 639;
    int count = x1 - x0 + 1;
    if (count <= 0) return;

    uint16_t *dst = (uint16_t *)fb_addr + (y * 640) + x0;
    uint32_t c32 = (uint32_t)color | ((uint32_t)color << 16);

    /* Align destination to 4 bytes */
    if (((uintptr_t)dst & 2) && count > 0) {
        *dst++ = color;
        count--;
    }

    uint32_t *dst32 = (uint32_t *)dst;
    int count32 = count >> 1;

    /* Fast 32-bit unrolled burst writes (write-only, zero VRAM read latency) */
    while (count32 >= 4) {
        dst32[0] = c32;
        dst32[1] = c32;
        dst32[2] = c32;
        dst32[3] = c32;
        dst32 += 4;
        count32 -= 4;
    }
    while (count32 > 0) {
        *dst32++ = c32;
        count32--;
    }
    if (count & 1) {
        *(uint16_t *)dst32 = color;
    }
}

/* Scanline edge-walking triangle rasterizer (flat) */
void rasterizer_draw_triangle(uint32_t fb_addr,
                              int x0, int y0,
                              int x1, int y1,
                              int x2, int y2,
                              uint16_t color)
{
    /* Bounding box cull */
    int min_x = x0; if (x1 < min_x) min_x = x1; if (x2 < min_x) min_x = x2;
    int max_x = x0; if (x1 > max_x) max_x = x1; if (x2 > max_x) max_x = x2;
    int min_y = y0; if (y1 < min_y) min_y = y1; if (y2 < min_y) min_y = y2;
    int max_y = y0; if (y1 > max_y) max_y = y1; if (y2 > max_y) max_y = y2;

    if (max_x < 0 || min_x >= 640 || max_y < 0 || min_y >= 480) return;

    /* Sort vertices by Y ascending (y0 <= y1 <= y2) */
    if (y0 > y1) { int tx = x0; x0 = x1; x1 = tx; int ty = y0; y0 = y1; y1 = ty; }
    if (y0 > y2) { int tx = x0; x0 = x2; x2 = tx; int ty = y0; y0 = y2; y2 = ty; }
    if (y1 > y2) { int tx = x1; x1 = x2; x2 = tx; int ty = y1; y1 = y2; y2 = ty; }

    int total_height = y2 - y0;
    if (total_height == 0) return;

    int32_t dx02 = sdiv32((x2 - x0) << 16, total_height);
    int32_t cur_xa = x0 << 16;

    /* Top half (y0 .. y1) */
    int h1 = y1 - y0;
    if (h1 > 0) {
        int32_t dx01 = sdiv32((x1 - x0) << 16, h1);
        int32_t cur_xb = x0 << 16;
        int start_y = (y0 < 0) ? 0 : y0;
        int end_y   = (y1 >= 480) ? 479 : (y1 - 1);

        if (y0 < 0) {
            cur_xa += dx02 * (-y0);
            cur_xb += dx01 * (-y0);
        }

        for (int y = start_y; y <= end_y; y++) {
            rasterizer_draw_span(fb_addr, y, cur_xa >> 16, cur_xb >> 16, color);
            cur_xa += dx02;
            cur_xb += dx01;
        }
    }

    /* Bottom half (y1 .. y2) */
    int h2 = y2 - y1;
    if (h2 > 0) {
        int32_t dx12 = sdiv32((x2 - x1) << 16, h2);
        int32_t cur_xb = x1 << 16;
        int start_y = (y1 < 0) ? 0 : y1;
        int end_y   = (y2 >= 480) ? 479 : y2;

        if (y1 < 0) {
            cur_xb += dx12 * (-y1);
        }

        for (int y = start_y; y <= end_y; y++) {
            rasterizer_draw_span(fb_addr, y, cur_xa >> 16, cur_xb >> 16, color);
            cur_xa += dx02;
            cur_xb += dx12;
        }
    }
}

/* Gouraud Interpolated Horizontal Span Filler */
void rasterizer_draw_span_gouraud(uint32_t fb_addr, int y,
                                  int x0, int x1,
                                  int32_t r0, int32_t g0, int32_t b0,
                                  int32_t r1, int32_t g1, int32_t b1)
{
    if (y < 0 || y >= 480) return;
    if (x0 > x1) {
        int tx = x0; x0 = x1; x1 = tx;
        int32_t tr = r0; r0 = r1; r1 = tr;
        int32_t tg = g0; g0 = g1; g1 = tg;
        int32_t tb = b0; b0 = b1; b1 = tb;
    }

    int span_len = x1 - x0;
    if (span_len <= 0) {
        if (x0 >= 0 && x0 < 640) {
            uint16_t *dst = (uint16_t *)fb_addr + (y * 640) + x0;
            int r = r0 >> 16; if (r < 0) r = 0; if (r > 31) r = 31;
            int g = g0 >> 16; if (g < 0) g = 0; if (g > 63) g = 63;
            int b = b0 >> 16; if (b < 0) b = 0; if (b > 31) b = 31;
            *dst = (uint16_t)((r << 11) | (g << 5) | b);
        }
        return;
    }

    int32_t dr = sdiv32(r1 - r0, span_len);
    int32_t dg = sdiv32(g1 - g0, span_len);
    int32_t db = sdiv32(b1 - b0, span_len);

    int start_x = x0;
    int end_x = x1;
    int32_t cur_r = r0;
    int32_t cur_g = g0;
    int32_t cur_b = b0;

    if (start_x < 0) {
        int skip = -start_x;
        cur_r += dr * skip;
        cur_g += dg * skip;
        cur_b += db * skip;
        start_x = 0;
    }
    if (end_x >= 640) end_x = 639;
    if (start_x > end_x) return;

    uint16_t *dst = (uint16_t *)fb_addr + (y * 640) + start_x;
    int count = end_x - start_x + 1;

    for (int i = 0; i < count; i++) {
        int r = cur_r >> 16; if (r < 0) r = 0; if (r > 31) r = 31;
        int g = cur_g >> 16; if (g < 0) g = 0; if (g > 63) g = 63;
        int b = cur_b >> 16; if (b < 0) b = 0; if (b > 31) b = 31;

        dst[i] = (uint16_t)((r << 11) | (g << 5) | b);
        cur_r += dr;
        cur_g += dg;
        cur_b += db;
    }
}

/* Smooth Gouraud Shaded Triangle Rasterizer */
void rasterizer_draw_triangle_gouraud(uint32_t fb_addr,
                                      int x0, int y0, uint16_t c0,
                                      int x1, int y1, uint16_t c1,
                                      int x2, int y2, uint16_t c2)
{
    /* If all vertices have the exact same color, fast path */
    if (c0 == c1 && c1 == c2) {
        rasterizer_draw_triangle(fb_addr, x0, y0, x1, y1, x2, y2, c0);
        return;
    }

    /* Bounding box cull */
    int min_x = x0; if (x1 < min_x) min_x = x1; if (x2 < min_x) min_x = x2;
    int max_x = x0; if (x1 > max_x) max_x = x1; if (x2 > max_x) max_x = x2;
    int min_y = y0; if (y1 < min_y) min_y = y1; if (y2 < min_y) min_y = y2;
    int max_y = y0; if (y1 > max_y) max_y = y1; if (y2 > max_y) max_y = y2;

    if (max_x < 0 || min_x >= 640 || max_y < 0 || min_y >= 480) return;

    /* Sort vertices by Y ascending (y0 <= y1 <= y2) */
    if (y0 > y1) {
        int tx = x0; x0 = x1; x1 = tx; int ty = y0; y0 = y1; y1 = ty;
        uint16_t tc = c0; c0 = c1; c1 = tc;
    }
    if (y0 > y2) {
        int tx = x0; x0 = x2; x2 = tx; int ty = y0; y0 = y2; y2 = ty;
        uint16_t tc = c0; c0 = c2; c2 = tc;
    }
    if (y1 > y2) {
        int tx = x1; x1 = x2; x2 = tx; int ty = y1; y1 = y2; y2 = ty;
        uint16_t tc = c1; c1 = c2; c2 = tc;
    }

    int total_height = y2 - y0;
    if (total_height == 0) return;

    int32_t r0 = ((c0 >> 11) & 0x1F) << 16;
    int32_t g0 = ((c0 >> 5)  & 0x3F) << 16;
    int32_t b0 = ( c0        & 0x1F) << 16;

    int32_t r1 = ((c1 >> 11) & 0x1F) << 16;
    int32_t g1 = ((c1 >> 5)  & 0x3F) << 16;
    int32_t b1 = ( c1        & 0x1F) << 16;

    int32_t r2 = ((c2 >> 11) & 0x1F) << 16;
    int32_t g2 = ((c2 >> 5)  & 0x3F) << 16;
    int32_t b2 = ( c2        & 0x1F) << 16;

    int32_t dx02 = sdiv32((x2 - x0) << 16, total_height);
    int32_t dr02 = sdiv32(r2 - r0, total_height);
    int32_t dg02 = sdiv32(g2 - g0, total_height);
    int32_t db02 = sdiv32(b2 - b0, total_height);

    int32_t cur_xa = x0 << 16;
    int32_t cur_ra = r0, cur_ga = g0, cur_ba = b0;

    /* Top half (y0 .. y1) */
    int h1 = y1 - y0;
    if (h1 > 0) {
        int32_t dx01 = sdiv32((x1 - x0) << 16, h1);
        int32_t dr01 = sdiv32(r1 - r0, h1);
        int32_t dg01 = sdiv32(g1 - g0, h1);
        int32_t db01 = sdiv32(b1 - b0, h1);

        int32_t cur_xb = x0 << 16;
        int32_t cur_rb = r0, cur_gb = g0, cur_bb = b0;

        int start_y = (y0 < 0) ? 0 : y0;
        int end_y   = (y1 >= 480) ? 479 : (y1 - 1);

        if (y0 < 0) {
            int skip = -y0;
            cur_xa += dx02 * skip;
            cur_ra += dr02 * skip;
            cur_ga += dg02 * skip;
            cur_ba += db02 * skip;

            cur_xb += dx01 * skip;
            cur_rb += dr01 * skip;
            cur_gb += dg01 * skip;
            cur_bb += db01 * skip;
        }

        for (int y = start_y; y <= end_y; y++) {
            rasterizer_draw_span_gouraud(fb_addr, y,
                                         cur_xa >> 16, cur_xb >> 16,
                                         cur_ra, cur_ga, cur_ba,
                                         cur_rb, cur_gb, cur_bb);
            cur_xa += dx02; cur_ra += dr02; cur_ga += dg02; cur_ba += db02;
            cur_xb += dx01; cur_rb += dr01; cur_gb += dg01; cur_bb += db01;
        }
    }

    /* Bottom half (y1 .. y2) */
    int h2 = y2 - y1;
    if (h2 > 0) {
        int32_t dx12 = sdiv32((x2 - x1) << 16, h2);
        int32_t dr12 = sdiv32(r2 - r1, h2);
        int32_t dg12 = sdiv32(g2 - g1, h2);
        int32_t db12 = sdiv32(b2 - b1, h2);

        int32_t cur_xb = x1 << 16;
        int32_t cur_rb = r1, cur_gb = g1, cur_bb = b1;

        int start_y = (y1 < 0) ? 0 : y1;
        int end_y   = (y2 >= 480) ? 479 : y2;

        if (y1 < 0) {
            int skip = -y1;
            cur_xb += dx12 * skip;
            cur_rb += dr12 * skip;
            cur_gb += dg12 * skip;
            cur_bb += db12 * skip;
        }

        for (int y = start_y; y <= end_y; y++) {
            rasterizer_draw_span_gouraud(fb_addr, y,
                                         cur_xa >> 16, cur_xb >> 16,
                                         cur_ra, cur_ga, cur_ba,
                                         cur_rb, cur_gb, cur_bb);
            cur_xa += dx02; cur_ra += dr02; cur_ga += dg02; cur_ba += db02;
            cur_xb += dx12; cur_rb += dr12; cur_gb += dg12; cur_bb += db12;
        }
    }
}

static const uint16_t recip_1024[1024] = {
    0, 65535, 32768, 21845, 16384, 13107, 10922, 9362, 8192, 7281, 6553, 5957, 5461, 5041, 4681, 4369,
    4096, 3855, 3640, 3449, 3276, 3120, 2978, 2849, 2730, 2621, 2520, 2427, 2340, 2259, 2184, 2114,
    2048, 1985, 1927, 1872, 1820, 1771, 1724, 1680, 1638, 1598, 1560, 1524, 1489, 1456, 1424, 1394,
    1365, 1337, 1310, 1285, 1260, 1236, 1213, 1191, 1170, 1149, 1129, 1110, 1092, 1074, 1057, 1040,
    1024, 1008, 992, 978, 963, 949, 936, 923, 910, 897, 885, 873, 862, 851, 840, 829,
    819, 809, 799, 789, 780, 771, 762, 753, 744, 736, 728, 720, 712, 704, 697, 689,
    682, 675, 668, 661, 655, 648, 642, 636, 630, 624, 618, 612, 606, 601, 595, 590,
    585, 579, 574, 569, 564, 560, 555, 550, 546, 541, 537, 532, 528, 524, 520, 516,
    512, 508, 504, 500, 496, 492, 489, 485, 481, 478, 474, 471, 468, 464, 461, 458,
    455, 451, 448, 445, 442, 439, 436, 434, 431, 428, 425, 422, 420, 417, 414, 412,
    409, 407, 404, 402, 399, 397, 394, 392, 390, 387, 385, 383, 381, 378, 376, 374,
    372, 370, 368, 366, 364, 362, 360, 358, 356, 354, 352, 350, 348, 346, 344, 343,
    341, 339, 337, 336, 334, 332, 330, 329, 327, 326, 324, 322, 321, 319, 318, 316,
    315, 313, 312, 310, 309, 307, 306, 304, 303, 302, 300, 299, 297, 296, 295, 293,
    292, 291, 289, 288, 287, 286, 284, 283, 282, 281, 280, 278, 277, 276, 275, 274,
    273, 271, 270, 269, 268, 267, 266, 265, 264, 263, 262, 261, 260, 259, 258, 257,
    256, 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241,
    240, 240, 239, 238, 237, 236, 235, 234, 234, 233, 232, 231, 230, 229, 229, 228,
    227, 226, 225, 225, 224, 223, 222, 222, 221, 220, 219, 219, 218, 217, 217, 216,
    215, 214, 214, 213, 212, 212, 211, 210, 210, 209, 208, 208, 207, 206, 206, 205,
    204, 204, 203, 202, 202, 201, 201, 200, 199, 199, 198, 197, 197, 196, 196, 195,
    195, 194, 193, 193, 192, 192, 191, 191, 190, 189, 189, 188, 188, 187, 187, 186,
    186, 185, 185, 184, 184, 183, 183, 182, 182, 181, 181, 180, 180, 179, 179, 178,
    178, 177, 177, 176, 176, 175, 175, 174, 174, 173, 173, 172, 172, 172, 171, 171,
    170, 170, 169, 169, 168, 168, 168, 167, 167, 166, 166, 165, 165, 165, 164, 164,
    163, 163, 163, 162, 162, 161, 161, 161, 160, 160, 159, 159, 159, 158, 158, 157,
    157, 157, 156, 156, 156, 155, 155, 154, 154, 154, 153, 153, 153, 152, 152, 152,
    151, 151, 151, 150, 150, 149, 149, 149, 148, 148, 148, 147, 147, 147, 146, 146,
    146, 145, 145, 145, 144, 144, 144, 144, 143, 143, 143, 142, 142, 142, 141, 141,
    141, 140, 140, 140, 140, 139, 139, 139, 138, 138, 138, 137, 137, 137, 137, 136,
    136, 136, 135, 135, 135, 135, 134, 134, 134, 134, 133, 133, 133, 132, 132, 132,
    132, 131, 131, 131, 131, 130, 130, 130, 130, 129, 129, 129, 129, 128, 128, 128,
    128, 127, 127, 127, 127, 126, 126, 126, 126, 125, 125, 125, 125, 124, 124, 124,
    124, 123, 123, 123, 123, 122, 122, 122, 122, 122, 121, 121, 121, 121, 120, 120,
    120, 120, 120, 119, 119, 119, 119, 118, 118, 118, 118, 118, 117, 117, 117, 117,
    117, 116, 116, 116, 116, 115, 115, 115, 115, 115, 114, 114, 114, 114, 114, 113,
    113, 113, 113, 113, 112, 112, 112, 112, 112, 112, 111, 111, 111, 111, 111, 110,
    110, 110, 110, 110, 109, 109, 109, 109, 109, 109, 108, 108, 108, 108, 108, 107,
    107, 107, 107, 107, 107, 106, 106, 106, 106, 106, 106, 105, 105, 105, 105, 105,
    105, 104, 104, 104, 104, 104, 104, 103, 103, 103, 103, 103, 103, 102, 102, 102,
    102, 102, 102, 101, 101, 101, 101, 101, 101, 100, 100, 100, 100, 100, 100, 100,
    99, 99, 99, 99, 99, 99, 98, 98, 98, 98, 98, 98, 98, 97, 97, 97,
    97, 97, 97, 97, 96, 96, 96, 96, 96, 96, 96, 95, 95, 95, 95, 95,
    95, 95, 94, 94, 94, 94, 94, 94, 94, 94, 93, 93, 93, 93, 93, 93,
    93, 92, 92, 92, 92, 92, 92, 92, 92, 91, 91, 91, 91, 91, 91, 91,
    91, 90, 90, 90, 90, 90, 90, 90, 90, 89, 89, 89, 89, 89, 89, 89,
    89, 88, 88, 88, 88, 88, 88, 88, 88, 87, 87, 87, 87, 87, 87, 87,
    87, 87, 86, 86, 86, 86, 86, 86, 86, 86, 86, 85, 85, 85, 85, 85,
    85, 85, 85, 85, 84, 84, 84, 84, 84, 84, 84, 84, 84, 83, 83, 83,
    83, 83, 83, 83, 83, 83, 82, 82, 82, 82, 82, 82, 82, 82, 82, 82,
    81, 81, 81, 81, 81, 81, 81, 81, 81, 81, 80, 80, 80, 80, 80, 80,
    80, 80, 80, 80, 79, 79, 79, 79, 79, 79, 79, 79, 79, 79, 78, 78,
    78, 78, 78, 78, 78, 78, 78, 78, 78, 77, 77, 77, 77, 77, 77, 77,
    77, 77, 77, 77, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 76, 75,
    75, 75, 75, 75, 75, 75, 75, 75, 75, 75, 74, 74, 74, 74, 74, 74,
    74, 74, 74, 74, 74, 74, 73, 73, 73, 73, 73, 73, 73, 73, 73, 73,
    73, 73, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 72, 71,
    71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 71, 70, 70, 70, 70,
    70, 70, 70, 70, 70, 70, 70, 70, 70, 69, 69, 69, 69, 69, 69, 69,
    69, 69, 69, 69, 69, 69, 68, 68, 68, 68, 68, 68, 68, 68, 68, 68,
    68, 68, 68, 68, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67, 67,
    67, 67, 67, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66, 66,
    66, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65,
    65, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
};

/* 1280x960 Fast Solid Filled Span Filler with unrolled 32-bit burst writes */
static inline void rasterizer_draw_span_ssaa_flat(uint32_t fb_addr, int y, int x0, int x1, uint16_t color)
{
    if (y < 0 || y >= 960) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= 1280) x1 = 1279;
    int count = x1 - x0 + 1;
    if (count <= 0) return;

    uint16_t *dst = (uint16_t *)fb_addr + (y * 1280) + x0;
    uint32_t c32 = (uint32_t)color | ((uint32_t)color << 16);

    /* Align destination to 4 bytes */
    if (((uintptr_t)dst & 2) && count > 0) {
        *dst++ = color;
        count--;
    }

    uint32_t *dst32 = (uint32_t *)dst;
    int count32 = count >> 1;

    /* Fast 32-bit unrolled burst writes (write-only, zero SDRAM read latency) */
    while (count32 >= 4) {
        dst32[0] = c32;
        dst32[1] = c32;
        dst32[2] = c32;
        dst32[3] = c32;
        dst32 += 4;
        count32 -= 4;
    }
    while (count32 > 0) {
        *dst32++ = c32;
        count32--;
    }
    if (count & 1) {
        *(uint16_t *)dst32 = color;
    }
}

/* 1280x960 Flat SSAA Triangle Rasterizer */
static void rasterizer_draw_triangle_ssaa_flat(uint32_t fb_addr,
                                              int x0, int y0,
                                              int x1, int y1,
                                              int x2, int y2,
                                              uint16_t color)
{
    /* Bounding box cull */
    int min_x = x0; if (x1 < min_x) min_x = x1; if (x2 < min_x) min_x = x2;
    int max_x = x0; if (x1 > max_x) max_x = x1; if (x2 > max_x) max_x = x2;
    int min_y = y0; if (y1 < min_y) min_y = y1; if (y2 < min_y) min_y = y2;
    int max_y = y0; if (y1 > max_y) max_y = y1; if (y2 > max_y) max_y = y2;

    if (max_x < 0 || min_x >= 1280 || max_y < 0 || min_y >= 960) return;

    /* Sort vertices by Y ascending (y0 <= y1 <= y2) */
    if (y0 > y1) { int tx = x0; x0 = x1; x1 = tx; int ty = y0; y0 = y1; y1 = ty; }
    if (y0 > y2) { int tx = x0; x0 = x2; x2 = tx; int ty = y0; y0 = y2; y2 = ty; }
    if (y1 > y2) { int tx = x1; x1 = x2; x2 = tx; int ty = y1; y1 = y2; y2 = ty; }

    int total_height = y2 - y0;
    if (total_height == 0) return;

    uint32_t inv_tot = (total_height < 1024) ? recip_1024[total_height] : (uint32_t)sdiv32(65536, total_height);
    int32_t dx02 = (x2 - x0) * (int32_t)inv_tot;
    int32_t cur_xa = x0 << 16;

    /* Top half (y0 .. y1) */
    int h1 = y1 - y0;
    if (h1 > 0) {
        uint32_t inv_h1 = (h1 < 1024) ? recip_1024[h1] : (uint32_t)sdiv32(65536, h1);
        int32_t dx01 = (x1 - x0) * (int32_t)inv_h1;
        int32_t cur_xb = x0 << 16;
        int start_y = (y0 < 0) ? 0 : y0;
        int end_y   = (y1 >= 960) ? 959 : (y1 - 1);

        if (y0 < 0) {
            int skip = -y0;
            cur_xa += dx02 * skip;
            cur_xb += dx01 * skip;
        }

        for (int y = start_y; y <= end_y; y++) {
            rasterizer_draw_span_ssaa_flat(fb_addr, y, cur_xa >> 16, cur_xb >> 16, color);
            cur_xa += dx02;
            cur_xb += dx01;
        }
    }

    /* Bottom half (y1 .. y2) */
    int h2 = y2 - y1;
    if (h2 > 0) {
        uint32_t inv_h2 = (h2 < 1024) ? recip_1024[h2] : (uint32_t)sdiv32(65536, h2);
        int32_t dx12 = (x2 - x1) * (int32_t)inv_h2;
        int32_t cur_xb = x1 << 16;
        int start_y = (y1 < 0) ? 0 : y1;
        int end_y   = (y2 >= 960) ? 959 : y2;

        if (y1 < 0) {
            int skip = -y1;
            cur_xb += dx12 * skip;
        }

        for (int y = start_y; y <= end_y; y++) {
            rasterizer_draw_span_ssaa_flat(fb_addr, y, cur_xa >> 16, cur_xb >> 16, color);
            cur_xa += dx02;
            cur_xb += dx12;
        }
    }
}

/* 1280x960 Gouraud Interpolated Horizontal Span Filler (2x SSAA, 0-division, branchless 32-bit packed) */
static void rasterizer_draw_span_gouraud_ssaa(uint32_t fb_addr, int y,
                                              int x0, int x1,
                                              int32_t r0, int32_t g0, int32_t b0,
                                              int32_t r1, int32_t g1, int32_t b1)
{
    if (y < 0 || y >= 960) return;
    if (x0 > x1) {
        int tx = x0; x0 = x1; x1 = tx;
        int32_t tr = r0; r0 = r1; r1 = tr;
        int32_t tg = g0; g0 = g1; g1 = tg;
        int32_t tb = b0; b0 = b1; b1 = tb;
    }

    int span_len = x1 - x0;
    if (span_len <= 0) {
        if (x0 >= 0 && x0 < 1280) {
            uint16_t *dst = (uint16_t *)fb_addr + (y * 1280) + x0;
            int r = r0 >> 16;
            int g = g0 >> 16;
            int b = b0 >> 16;
            *dst = (uint16_t)((r << 11) | (g << 5) | b);
        }
        return;
    }

    int start_x = x0;
    int end_x = x1;
    if (start_x < 0) start_x = 0;
    if (end_x >= 1280) end_x = 1279;
    if (start_x > end_x) return;

    uint16_t *dst = (uint16_t *)fb_addr + (y * 1280) + start_x;
    int count = end_x - start_x + 1;

    /* Fast path: flat / uniform color span with unrolled 32-bit burst writes */
    if (r0 == r1 && g0 == g1 && b0 == b1) {
        int r = r0 >> 16;
        int g = g0 >> 16;
        int b = b0 >> 16;
        uint16_t col16 = (uint16_t)((r << 11) | (g << 5) | b);
        uint32_t col32 = (uint32_t)col16 | ((uint32_t)col16 << 16);

        if (((uintptr_t)dst & 2) && count > 0) {
            *dst++ = col16;
            count--;
        }

        uint32_t *dst32 = (uint32_t *)dst;
        int count32 = count >> 1;
        while (count32 >= 4) {
            dst32[0] = col32; dst32[1] = col32; dst32[2] = col32; dst32[3] = col32;
            dst32 += 4;
            count32 -= 4;
        }
        while (count32 > 0) {
            *dst32++ = col32;
            count32--;
        }
        if (count & 1) {
            *(uint16_t *)dst32 = col16;
        }
        return;
    }

    /* Fast 1-cycle reciprocal multiply instead of 3 division calls */
    uint32_t inv_len = (span_len < 1024) ? recip_1024[span_len] : (uint32_t)sdiv32(65536, span_len);
    int32_t dr = ((r1 - r0) * (int32_t)inv_len) >> 16;
    int32_t dg = ((g1 - g0) * (int32_t)inv_len) >> 16;
    int32_t db = ((b1 - b0) * (int32_t)inv_len) >> 16;

    int32_t cur_r = r0;
    int32_t cur_g = g0;
    int32_t cur_b = b0;

    if (x0 < 0) {
        int skip = -x0;
        cur_r += dr * skip;
        cur_g += dg * skip;
        cur_b += db * skip;
    }

    /* Align destination to 4 bytes */
    if (((uintptr_t)dst & 2) && count > 0) {
        int r = cur_r >> 16;
        int g = cur_g >> 16;
        int b = cur_b >> 16;
        *dst++ = (uint16_t)((r << 11) | (g << 5) | b);
        cur_r += dr; cur_g += dg; cur_b += db;
        count--;
    }

    uint32_t *dst32 = (uint32_t *)dst;
    int count32 = count >> 1;

    for (int i = 0; i < count32; i++) {
        int r0_ = cur_r >> 16;
        int g0_ = cur_g >> 16;
        int b0_ = cur_b >> 16;
        uint32_t p0 = (uint32_t)((r0_ << 11) | (g0_ << 5) | b0_);

        cur_r += dr; cur_g += dg; cur_b += db;

        int r1_ = cur_r >> 16;
        int g1_ = cur_g >> 16;
        int b1_ = cur_b >> 16;
        uint32_t p1 = (uint32_t)((r1_ << 11) | (g1_ << 5) | b1_);

        cur_r += dr; cur_g += dg; cur_b += db;

        dst32[i] = p0 | (p1 << 16);
    }

    if (count & 1) {
        int r = cur_r >> 16;
        int g = cur_g >> 16;
        int b = cur_b >> 16;
        ((uint16_t *)(dst32 + count32))[0] = (uint16_t)((r << 11) | (g << 5) | b);
    }
}

/* 1280x960 Smooth Gouraud Shaded Triangle Rasterizer (2x SSAA) */
void rasterizer_draw_triangle_gouraud_ssaa(uint32_t fb_addr,
                                           int x0, int y0, uint16_t c0,
                                           int x1, int y1, uint16_t c1,
                                           int x2, int y2, uint16_t c2)
{
    /* If all vertices have identical color, route to fast flat rasterizer */
    if (c0 == c1 && c1 == c2) {
        rasterizer_draw_triangle_ssaa_flat(fb_addr, x0, y0, x1, y1, x2, y2, c0);
        return;
    }

    /* Bounding box cull against 1280x960 viewport */
    int min_x = x0; if (x1 < min_x) min_x = x1; if (x2 < min_x) min_x = x2;
    int max_x = x0; if (x1 > max_x) max_x = x1; if (x2 > max_x) max_x = x2;
    int min_y = y0; if (y1 < min_y) min_y = y1; if (y2 < min_y) min_y = y2;
    int max_y = y0; if (y1 > max_y) max_y = y1; if (y2 > max_y) max_y = y2;

    if (max_x < 0 || min_x >= 1280 || max_y < 0 || min_y >= 960) return;

    /* Sort vertices by Y ascending (y0 <= y1 <= y2) */
    if (y0 > y1) {
        int tx = x0; x0 = x1; x1 = tx; int ty = y0; y0 = y1; y1 = ty;
        uint16_t tc = c0; c0 = c1; c1 = tc;
    }
    if (y0 > y2) {
        int tx = x0; x0 = x2; x2 = tx; int ty = y0; y0 = y2; y2 = ty;
        uint16_t tc = c0; c0 = c2; c2 = tc;
    }
    if (y1 > y2) {
        int tx = x1; x1 = x2; x2 = tx; int ty = y1; y1 = y2; y2 = ty;
        uint16_t tc = c1; c1 = c2; c2 = tc;
    }

    int total_height = y2 - y0;
    if (total_height == 0) return;

    int32_t r0 = ((c0 >> 11) & 0x1F) << 16;
    int32_t g0 = ((c0 >> 5)  & 0x3F) << 16;
    int32_t b0 = ( c0        & 0x1F) << 16;

    int32_t r1 = ((c1 >> 11) & 0x1F) << 16;
    int32_t g1 = ((c1 >> 5)  & 0x3F) << 16;
    int32_t b1 = ( c1        & 0x1F) << 16;

    int32_t r2 = ((c2 >> 11) & 0x1F) << 16;
    int32_t g2 = ((c2 >> 5)  & 0x3F) << 16;
    int32_t b2 = ( c2        & 0x1F) << 16;

    uint32_t inv_tot = (total_height < 1024) ? recip_1024[total_height] : (uint32_t)sdiv32(65536, total_height);
    int32_t dx02 = (x2 - x0) * (int32_t)inv_tot;
    int32_t dr02 = ((r2 - r0) * (int32_t)inv_tot) >> 16;
    int32_t dg02 = ((g2 - g0) * (int32_t)inv_tot) >> 16;
    int32_t db02 = ((b2 - b0) * (int32_t)inv_tot) >> 16;

    int32_t cur_xa = x0 << 16;
    int32_t cur_ra = r0, cur_ga = g0, cur_ba = b0;

    /* Top half (y0 .. y1) */
    int h1 = y1 - y0;
    if (h1 > 0) {
        uint32_t inv_h1 = (h1 < 1024) ? recip_1024[h1] : (uint32_t)sdiv32(65536, h1);
        int32_t dx01 = (x1 - x0) * (int32_t)inv_h1;
        int32_t dr01 = ((r1 - r0) * (int32_t)inv_h1) >> 16;
        int32_t dg01 = ((g1 - g0) * (int32_t)inv_h1) >> 16;
        int32_t db01 = ((b1 - b0) * (int32_t)inv_h1) >> 16;

        int32_t cur_xb = x0 << 16;
        int32_t cur_rb = r0, cur_gb = g0, cur_bb = b0;

        int start_y = (y0 < 0) ? 0 : y0;
        int end_y   = (y1 >= 960) ? 959 : (y1 - 1);

        if (y0 < 0) {
            int skip = -y0;
            cur_xa += dx02 * skip; cur_ra += dr02 * skip; cur_ga += dg02 * skip; cur_ba += db02 * skip;
            cur_xb += dx01 * skip; cur_rb += dr01 * skip; cur_gb += dg01 * skip; cur_bb += db01 * skip;
        }

        for (int y = start_y; y <= end_y; y++) {
            rasterizer_draw_span_gouraud_ssaa(fb_addr, y,
                                              cur_xa >> 16, cur_xb >> 16,
                                              cur_ra, cur_ga, cur_ba,
                                              cur_rb, cur_gb, cur_bb);
            cur_xa += dx02; cur_ra += dr02; cur_ga += dg02; cur_ba += db02;
            cur_xb += dx01; cur_rb += dr01; cur_gb += dg01; cur_bb += db01;
        }
    }

    /* Bottom half (y1 .. y2) */
    int h2 = y2 - y1;
    if (h2 > 0) {
        uint32_t inv_h2 = (h2 < 1024) ? recip_1024[h2] : (uint32_t)sdiv32(65536, h2);
        int32_t dx12 = (x2 - x1) * (int32_t)inv_h2;
        int32_t dr12 = ((r2 - r1) * (int32_t)inv_h2) >> 16;
        int32_t dg12 = ((g2 - g1) * (int32_t)inv_h2) >> 16;
        int32_t db12 = ((b2 - b1) * (int32_t)inv_h2) >> 16;

        int32_t cur_xb = x1 << 16;
        int32_t cur_rb = r1, cur_gb = g1, cur_bb = b1;

        int start_y = (y1 < 0) ? 0 : y1;
        int end_y   = (y2 >= 960) ? 959 : y2;

        if (y1 < 0) {
            int skip = -y1;
            cur_xb += dx12 * skip; cur_rb += dr12 * skip; cur_gb += dg12 * skip; cur_bb += db12 * skip;
        }

        for (int y = start_y; y <= end_y; y++) {
            rasterizer_draw_span_gouraud_ssaa(fb_addr, y,
                                              cur_xa >> 16, cur_xb >> 16,
                                              cur_ra, cur_ga, cur_ba,
                                              cur_rb, cur_gb, cur_bb);
            cur_xa += dx02; cur_ra += dr02; cur_ga += dg02; cur_ba += db02;
            cur_xb += dx12; cur_rb += dr12; cur_gb += dg12; cur_bb += db12;
        }
    }
}

/* Fast lighting calculation using pre-computed dot product */
uint16_t rasterizer_calc_lighting_fast(float dot, uint16_t base_color)
{
    if (dot < -1.0f) dot = -1.0f;
    if (dot > 1.0f)  dot = 1.0f;

    int r_base = (base_color >> 11) & 0x1F;
    int g_base = (base_color >> 5)  & 0x3F;
    int b_base =  base_color        & 0x1F;

    int factor = 220 + (int)(dot * 36.0f);
    if (factor < 175) factor = 175;
    if (factor > 256) factor = 256;

    int r = (r_base * factor) >> 8;
    int g = (g_base * factor) >> 8;
    int b = (b_base * factor) >> 8;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Authentic Dreamcast Smooth Studio Shading (Preserves pure material hue without yellowing or discoloration) */
uint16_t rasterizer_calc_lighting(float nx, float ny, float nz, float n2, uint16_t base_color)
{
    (void)n2;
    /* Soft studio lighting: 80% ambient + 20% directional (factor 175..256 in 8.8 fixed point) */
    float dot = nx * (-0.267f) + ny * 0.535f + nz * 0.802f;
    return rasterizer_calc_lighting_fast(dot, base_color);
}

