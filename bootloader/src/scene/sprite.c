#include "sprite.h"
#include <stddef.h>

/* 2D Alpha-Blended ARGB4444 Sprite Blitter */
void sprite_blit_argb4444(uint32_t fb_addr,
                          int dest_x,
                          int dest_y,
                          int width,
                          int height,
                          const uint16_t *pixels,
                          int global_alpha)
{
    if (global_alpha <= 0 || width <= 0 || height <= 0 || !pixels) return;
    volatile uint16_t *fb = (volatile uint16_t *)fb_addr;

    for (int y = 0; y < height; y++) {
        int py = dest_y + y;
        if (py < 0 || py >= 330) continue; /* Mask line at Y=330: hide below baseline until popped up */
        volatile uint16_t *dst_row = fb + (py * 640);
        const uint16_t *src_row = pixels + (y * width);

        for (int x = 0; x < width; x++) {
            int px = dest_x + x;
            if (px < 0 || px >= 640) continue;

            uint16_t p = src_row[x];
            uint8_t a = (p >> 12) & 0x0F;
            if (a == 0) continue;

            int eff_a = (a * 17 * global_alpha) >> 8;
            if (eff_a <= 0) continue;

            int r4 = (p >> 8) & 0x0F;
            int g4 = (p >> 4) & 0x0F;
            int b4 = p & 0x0F;

            int r_src = (r4 << 1) | (r4 >> 3);   /* 0..31 (5-bit) */
            int g_src = (g4 << 2) | (g4 >> 2);   /* 0..63 (6-bit) */
            int b_src = (b4 << 1) | (b4 >> 3);   /* 0..31 (5-bit) */
            uint16_t src_col = (uint16_t)((r_src << 11) | (g_src << 5) | b_src);

            if (eff_a >= 250) {
                dst_row[px] = src_col;
            } else {
                uint16_t dst_col = dst_row[px];
                int r_dst = (dst_col >> 11) & 0x1F;
                int g_dst = (dst_col >> 5)  & 0x3F;
                int b_dst = dst_col & 0x1F;

                int r = r_dst + (((r_src - r_dst) * eff_a) >> 8);
                int g = g_dst + (((g_src - g_dst) * eff_a) >> 8);
                int b = b_dst + (((b_src - b_dst) * eff_a) >> 8);

                dst_row[px] = (uint16_t)((r << 11) | (g << 5) | b);
            }
        }
    }
}
