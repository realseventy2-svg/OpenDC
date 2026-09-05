#include "postprocess.h"
#include "scene_types.h"

/* Scratchpad line buffers at 0x8C11C000 (preserves resident STATE area) */
static uint16_t * const row_prev = (uint16_t *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x1C000);
static uint16_t * const row_curr = (uint16_t *)(BOOT_SCENE_SCRATCHPAD_BASE + 0x1C800);

/* High-Performance Dreamcast Sub-pixel Silhouette Reconstruction & Edge Anti-Aliasing Filter */
void postprocess_smooth_edges(uint32_t fb_addr, int min_x, int min_y, int max_x, int max_y, uint16_t bg_color)
{
    if (min_x < 2) min_x = 2;
    if (min_y < 2) min_y = 2;
    if (max_x > 637) max_x = 637;
    if (max_y > 477) max_y = 477;
    if (min_x >= max_x || min_y >= max_y) return;

    volatile uint16_t *fb = (volatile uint16_t *)fb_addr;

    int r_bg = (bg_color >> 11) & 0x1F;
    int g_bg = (bg_color >> 5)  & 0x3F;
    int b_bg =  bg_color        & 0x1F;

    /* Pre-fill initial row buffers */
    for (int x = min_x - 1; x <= max_x + 1; x++) {
        row_prev[x] = fb[(min_y - 1) * 640 + x];
        row_curr[x] = fb[min_y * 640 + x];
    }

    for (int y = min_y; y <= max_y; y++) {
        volatile uint16_t *row_dst = fb + (y * 640);
        volatile uint16_t *row_next = fb + ((y + 1) * 640);

        for (int x = min_x; x <= max_x; x++) {
            uint16_t c_orig = row_curr[x];
            uint16_t c_up   = row_prev[x];
            uint16_t c_dn   = row_next[x];
            uint16_t c_lf   = row_curr[x - 1];
            uint16_t c_rt   = row_curr[x + 1];

            if (c_orig != bg_color) {
                /* Foreground pixel: blend towards background if adjacent to background */
                int bg_count = (c_up == bg_color) + (c_dn == bg_color) + (c_lf == bg_color) + (c_rt == bg_color);
                if (bg_count > 0) {
                    int r_o = (c_orig >> 11) & 0x1F;
                    int g_o = (c_orig >> 5)  & 0x3F;
                    int b_o =  c_orig        & 0x1F;

                    int alpha_bg = bg_count * 52; /* ~20% per boundary neighbor */
                    int r = r_o + (((r_bg - r_o) * alpha_bg) >> 8);
                    int g = g_o + (((g_bg - g_o) * alpha_bg) >> 8);
                    int b = b_o + (((b_bg - b_o) * alpha_bg) >> 8);
                    row_dst[x] = (uint16_t)((r << 11) | (g << 5) | b);
                }
            } else {
                /* Background pixel: blend foreground color outward into background if adjacent to foreground */
                int fg_count = 0;
                int sum_r = 0, sum_g = 0, sum_b = 0;

                if (c_up != bg_color) {
                    fg_count++;
                    sum_r += (c_up >> 11) & 0x1F;
                    sum_g += (c_up >> 5)  & 0x3F;
                    sum_b +=  c_up        & 0x1F;
                }
                if (c_dn != bg_color) {
                    fg_count++;
                    sum_r += (c_dn >> 11) & 0x1F;
                    sum_g += (c_dn >> 5)  & 0x3F;
                    sum_b +=  c_dn        & 0x1F;
                }
                if (c_lf != bg_color) {
                    fg_count++;
                    sum_r += (c_lf >> 11) & 0x1F;
                    sum_g += (c_lf >> 5)  & 0x3F;
                    sum_b +=  c_lf        & 0x1F;
                }
                if (c_rt != bg_color) {
                    fg_count++;
                    sum_r += (c_rt >> 11) & 0x1F;
                    sum_g += (c_rt >> 5)  & 0x3F;
                    sum_b +=  c_rt        & 0x1F;
                }

                if (fg_count > 0) {
                    int avg_r, avg_g, avg_b;
                    if (fg_count == 1) {
                        avg_r = sum_r; avg_g = sum_g; avg_b = sum_b;
                    } else if (fg_count == 2) {
                        avg_r = sum_r >> 1; avg_g = sum_g >> 1; avg_b = sum_b >> 1;
                    } else if (fg_count == 3) {
                        avg_r = (sum_r * 85) >> 8; avg_g = (sum_g * 85) >> 8; avg_b = (sum_b * 85) >> 8;
                    } else {
                        avg_r = sum_r >> 2; avg_g = sum_g >> 2; avg_b = sum_b >> 2;
                    }

                    int alpha_fg = fg_count * 44; /* ~17% per boundary neighbor */
                    int r = r_bg + (((avg_r - r_bg) * alpha_fg) >> 8);
                    int g = g_bg + (((avg_g - g_bg) * alpha_fg) >> 8);
                    int b = b_bg + (((avg_b - b_bg) * alpha_fg) >> 8);
                    row_dst[x] = (uint16_t)((r << 11) | (g << 5) | b);
                }
            }
        }

        /* Slide row buffers: prev becomes current, current loads next unmodified scanline */
        for (int x = min_x - 1; x <= max_x + 1; x++) {
            row_prev[x] = row_curr[x];
            row_curr[x] = row_next[x];
        }
    }
}

/* Fast 32-bit burst clear of SSAA bounding box in SDRAM */
void postprocess_clear_ssaa_box(uint32_t ssaa_fb_addr, int min_x, int min_y, int max_x, int max_y, uint16_t bg_color)
{
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= 1280) max_x = 1279;
    if (max_y >= 960) max_y = 959;
    if (min_x > max_x || min_y > max_y) return;

    uint32_t c32 = (uint32_t)bg_color | ((uint32_t)bg_color << 16);
    int width = max_x - min_x + 1;

    for (int y = min_y; y <= max_y; y++) {
        uint16_t *dst = (uint16_t *)ssaa_fb_addr + (y * 1280) + min_x;
        int count = width;

        if (((uintptr_t)dst & 2) && count > 0) {
            *dst++ = bg_color;
            count--;
        }

        uint32_t *dst32 = (uint32_t *)dst;
        int count32 = count >> 1;

        while (count32 >= 4) {
            dst32[0] = c32; dst32[1] = c32; dst32[2] = c32; dst32[3] = c32;
            dst32 += 4;
            count32 -= 4;
        }
        while (count32 > 0) {
            *dst32++ = c32;
            count32--;
        }
        if (count & 1) {
            *(uint16_t *)dst32 = bg_color;
        }
    }
}

/* 2x2 SSAA Box Downsampling Resolve: 1280x960 (SDRAM) -> 640x480 (VRAM) with 32-bit burst writes */
void postprocess_resolve_2x_ssaa(uint32_t ssaa_fb_addr, uint32_t dst_fb_addr, int min_x, int min_y, int max_x, int max_y)
{
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= 640) max_x = 639;
    if (max_y >= 480) max_y = 479;
    if (min_x > max_x || min_y > max_y) return;

    const uint16_t *ssaa_base = (const uint16_t *)ssaa_fb_addr;
    uint16_t *dst_base = (uint16_t *)dst_fb_addr;

    for (int y = min_y; y <= max_y; y++) {
        const uint32_t *s_row0_32 = (const uint32_t *)(ssaa_base + ((y * 2) * 1280) + (min_x * 2));
        const uint32_t *s_row1_32 = (const uint32_t *)(ssaa_base + ((y * 2 + 1) * 1280) + (min_x * 2));
        uint16_t *d_row = dst_base + (y * 640) + min_x;

        int width = max_x - min_x + 1;
        int src_idx = 0;

        if (((uintptr_t)d_row & 2) && width > 0) {
            uint32_t pair0 = s_row0_32[src_idx];
            uint32_t pair1 = s_row1_32[src_idx];
            uint16_t p00 = (uint16_t)pair0;
            uint16_t p01 = (uint16_t)(pair0 >> 16);
            uint16_t p10 = (uint16_t)pair1;
            uint16_t p11 = (uint16_t)(pair1 >> 16);

            if (p00 == p01 && p01 == p10 && p10 == p11) {
                *d_row++ = p00;
            } else {
                int r = (((p00 >> 11) & 0x1F) + ((p01 >> 11) & 0x1F) + ((p10 >> 11) & 0x1F) + ((p11 >> 11) & 0x1F) + 2) >> 2;
                int g = (((p00 >> 5)  & 0x3F) + ((p01 >> 5)  & 0x3F) + ((p10 >> 5)  & 0x3F) + ((p11 >> 5)  & 0x3F) + 2) >> 2;
                int b = (( p00        & 0x1F) + ( p01        & 0x1F) + ( p10        & 0x1F) + ( p11        & 0x1F) + 2) >> 2;
                *d_row++ = (uint16_t)((r << 11) | (g << 5) | b);
            }
            src_idx++;
            width--;
        }

        uint32_t *d_row32 = (uint32_t *)d_row;
        int width32 = width >> 1;

        for (int i = 0; i < width32; i++) {
            uint32_t pair00 = s_row0_32[src_idx];
            uint32_t pair01 = s_row1_32[src_idx];
            uint32_t pair10 = s_row0_32[src_idx + 1];
            uint32_t pair11 = s_row1_32[src_idx + 1];
            src_idx += 2;

            uint16_t res0, res1;

            if (pair00 == pair01 && ((uint16_t)pair00 == (uint16_t)(pair00 >> 16))) {
                res0 = (uint16_t)pair00;
            } else {
                uint16_t p00 = (uint16_t)pair00;
                uint16_t p01 = (uint16_t)(pair00 >> 16);
                uint16_t p10 = (uint16_t)pair01;
                uint16_t p11 = (uint16_t)(pair01 >> 16);
                int r = (((p00 >> 11) & 0x1F) + ((p01 >> 11) & 0x1F) + ((p10 >> 11) & 0x1F) + ((p11 >> 11) & 0x1F) + 2) >> 2;
                int g = (((p00 >> 5)  & 0x3F) + ((p01 >> 5)  & 0x3F) + ((p10 >> 5)  & 0x3F) + ((p11 >> 5)  & 0x3F) + 2) >> 2;
                int b = (( p00        & 0x1F) + ( p01        & 0x1F) + ( p10        & 0x1F) + ( p11        & 0x1F) + 2) >> 2;
                res0 = (uint16_t)((r << 11) | (g << 5) | b);
            }

            if (pair10 == pair11 && ((uint16_t)pair10 == (uint16_t)(pair10 >> 16))) {
                res1 = (uint16_t)pair10;
            } else {
                uint16_t p02 = (uint16_t)pair10;
                uint16_t p03 = (uint16_t)(pair10 >> 16);
                uint16_t p12 = (uint16_t)pair11;
                uint16_t p13 = (uint16_t)(pair11 >> 16);
                int r = (((p02 >> 11) & 0x1F) + ((p03 >> 11) & 0x1F) + ((p12 >> 11) & 0x1F) + ((p13 >> 11) & 0x1F) + 2) >> 2;
                int g = (((p02 >> 5)  & 0x3F) + ((p03 >> 5)  & 0x3F) + ((p12 >> 5)  & 0x3F) + ((p13 >> 5)  & 0x3F) + 2) >> 2;
                int b = (( p02        & 0x1F) + ( p03        & 0x1F) + ( p12        & 0x1F) + ( p13        & 0x1F) + 2) >> 2;
                res1 = (uint16_t)((r << 11) | (g << 5) | b);
            }

            d_row32[i] = (uint32_t)res0 | ((uint32_t)res1 << 16);
        }

        if (width & 1) {
            uint32_t pair0 = s_row0_32[src_idx];
            uint32_t pair1 = s_row1_32[src_idx];
            uint16_t p00 = (uint16_t)pair0;
            uint16_t p01 = (uint16_t)(pair0 >> 16);
            uint16_t p10 = (uint16_t)pair1;
            uint16_t p11 = (uint16_t)(pair1 >> 16);

            if (p00 == p01 && p01 == p10 && p10 == p11) {
                ((uint16_t *)d_row32)[width - 1] = p00;
            } else {
                int r = (((p00 >> 11) & 0x1F) + ((p01 >> 11) & 0x1F) + ((p10 >> 11) & 0x1F) + ((p11 >> 11) & 0x1F) + 2) >> 2;
                int g = (((p00 >> 5)  & 0x3F) + ((p01 >> 5)  & 0x3F) + ((p10 >> 5)  & 0x3F) + ((p11 >> 5)  & 0x3F) + 2) >> 2;
                int b = (( p00        & 0x1F) + ( p01        & 0x1F) + ( p10        & 0x1F) + ( p11        & 0x1F) + 2) >> 2;
                ((uint16_t *)d_row32)[width - 1] = (uint16_t)((r << 11) | (g << 5) | b);
            }
        }
    }
}
