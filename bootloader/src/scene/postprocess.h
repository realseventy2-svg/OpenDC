#ifndef OPENDC_POSTPROCESS_H
#define OPENDC_POSTPROCESS_H

#include <stdint.h>

void postprocess_clear_ssaa_box(uint32_t ssaa_fb_addr, int min_x, int min_y, int max_x, int max_y, uint16_t bg_color);
void postprocess_resolve_2x_ssaa(uint32_t ssaa_fb_addr, uint32_t dst_fb_addr, int min_x, int min_y, int max_x, int max_y);
void postprocess_smooth_edges(uint32_t fb_addr, int min_x, int min_y, int max_x, int max_y, uint16_t bg_color);

#endif /* OPENDC_POSTPROCESS_H */
