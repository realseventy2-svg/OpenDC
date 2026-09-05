#include "postprocess.h"

/* 5-tap Sub-pixel Silhouette Edge Anti-Aliasing Filter */
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

    /* Previous scanline buffer to prevent write-after-read cascading */
    static uint16_t prev_row[640];
    for (int x = min_x - 1; x <= max_x + 1; x++) {
        prev_row[x] = fb[(min_y - 1) * 640 + x];
    }

    for (int y = min_y; y <= max_y; y++) {
        volatile uint16_t *row_curr = fb + (y * 640);
        volatile uint16_t *row_next = fb + ((y + 1) * 640);

        uint16_t left_orig = row_curr[min_x - 1];

        for (int x = min_x; x <= max_x; x++) {
            uint16_t c_orig = row_curr[x];
            uint16_t c_up   = prev_row[x];
            uint16_t c_dn   = row_next[x];
            uint16_t c_lf   = left_orig;
            uint16_t c_rt   = row_curr[x + 1];

            left_orig = c_orig;
            prev_row[x] = c_orig;

            if (c_orig != bg_color) {
                int bg_neighbors = (c_up == bg_color) + (c_dn == bg_color) + (c_lf == bg_color) + (c_rt == bg_color);
                if (bg_neighbors > 0) {
                    int r_o = (c_orig >> 11) & 0x1F;
                    int g_o = (c_orig >> 5)  & 0x3F;
                    int b_o =  c_orig        & 0x1F;

                    /* Smooth alpha blend against background: 1=~19%, 2=~38%, 3=~56%, 4=~75% */
                    int alpha_bg = bg_neighbors * 48;
                    int r = r_o + (((r_bg - r_o) * alpha_bg) >> 8);
                    int g = g_o + (((g_bg - g_o) * alpha_bg) >> 8);
                    int b = b_o + (((b_bg - b_o) * alpha_bg) >> 8);
                    row_curr[x] = (uint16_t)((r << 11) | (g << 5) | b);
                }
            }
        }
    }
}
