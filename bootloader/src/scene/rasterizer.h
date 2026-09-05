#ifndef OPENDC_RASTERIZER_H
#define OPENDC_RASTERIZER_H

#include <stdint.h>

void rasterizer_draw_span(uint32_t fb_addr, int y, int x0, int x1, uint16_t color);

void rasterizer_draw_triangle(uint32_t fb_addr,
                              int x0, int y0,
                              int x1, int y1,
                              int x2, int y2,
                              uint16_t color);

uint16_t rasterizer_calc_lighting(float nx, float ny, float nz, float n2, uint16_t base_color);

#endif /* OPENDC_RASTERIZER_H */
