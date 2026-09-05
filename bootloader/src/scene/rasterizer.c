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

/* Scanline edge-walking triangle rasterizer */
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

/* Authentic Dreamcast BIOS 2-tone palette shader with specular bevel glints */
uint16_t rasterizer_calc_lighting(float nx, float ny, float nz, float n2, uint16_t base_color)
{
    if (n2 <= 0.0001f) return base_color;

    /* Light from top-left front: (-0.355, 0.710, 0.609) */
    float dot = nx * (-0.355f) + ny * 0.710f + nz * 0.609f;
    float inv_len = 1.0f / __builtin_sqrtf(n2);
    float ndot = dot * inv_len;
    if (ndot < -1.0f) ndot = -1.0f;
    if (ndot > 1.0f)  ndot = 1.0f;

    int r_base = (base_color >> 11) & 0x1F;
    int g_base = (base_color >> 5)  & 0x3F;
    int b_base =  base_color        & 0x1F;

    int r, g, b;
    if (ndot < 0.0f) {
        /* Shadow crevice: Rich deep saturated shade (crimson for orange, deep blue for blue) */
        int t = 256 + (int)(ndot * 256.0f);
        if (t < 0) t = 0;

        int r_sh = (r_base * 3) >> 2;
        int g_sh = (g_base * 1) >> 2;
        int b_sh = (b_base * 1) >> 2;

        r = r_sh + (((r_base - r_sh) * t) >> 8);
        g = g_sh + (((g_base - g_sh) * t) >> 8);
        b = b_sh + (((b_base - b_sh) * t) >> 8);
    } else if (ndot < 0.65f) {
        /* Diffuse to Golden Highlight: Radiant warm illumination */
        int t = (int)(ndot * (256.0f / 0.65f));
        if (t > 256) t = 256;

        int r_hi = 31;
        int g_hi = g_base + (((63 - g_base) * 110) >> 8);
        int b_hi = b_base + 3;

        r = r_base + (((r_hi - r_base) * t) >> 8);
        g = g_base + (((g_hi - g_base) * t) >> 8);
        b = b_base + (((b_hi - b_base) * t) >> 8);
    } else {
        /* Specular Glint: Lustrous gleaming cream bevel highlight */
        int t = (int)((ndot - 0.65f) * (256.0f / 0.35f));
        if (t > 256) t = 256;

        int r_hi = 31;
        int g_hi = g_base + (((63 - g_base) * 110) >> 8);
        int b_hi = b_base + 3;

        int r_sp = 31;
        int g_sp = 58;
        int b_sp = 18;

        r = r_hi + (((r_sp - r_hi) * t) >> 8);
        g = g_hi + (((g_sp - g_hi) * t) >> 8);
        b = b_hi + (((b_sp - b_hi) * t) >> 8);
    }

    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;

    return (uint16_t)((r << 11) | (g << 5) | b);
}
