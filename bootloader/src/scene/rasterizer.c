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

/* Authentic Dreamcast Smooth Studio Shading (Preserves pure material hue without yellowing or discoloration) */
uint16_t rasterizer_calc_lighting(float nx, float ny, float nz, float n2, uint16_t base_color)
{
    if (n2 <= 0.0001f) return base_color;

    /* Light from front-top-left: (-0.267, 0.535, 0.802) normalized */
    float dot = nx * (-0.267f) + ny * 0.535f + nz * 0.802f;
    float inv_len = 1.0f / __builtin_sqrtf(n2);
    float ndot = dot * inv_len;
    if (ndot < -1.0f) ndot = -1.0f;
    if (ndot > 1.0f)  ndot = 1.0f;

    int r_base = (base_color >> 11) & 0x1F;
    int g_base = (base_color >> 5)  & 0x3F;
    int b_base =  base_color        & 0x1F;

    /* Soft studio lighting: 80% ambient + 20% directional (factor 205..256 in 8.8 fixed point) */
    int factor = 220 + (int)(ndot * 36.0f);
    if (factor < 175) factor = 175;
    if (factor > 256) factor = 256;

    int r = (r_base * factor) >> 8;
    int g = (g_base * factor) >> 8;
    int b = (b_base * factor) >> 8;

    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;

    return (uint16_t)((r << 11) | (g << 5) | b);
}
