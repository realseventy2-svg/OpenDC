#ifndef OPENDC_POSTPROCESS_H
#define OPENDC_POSTPROCESS_H

#include <stdint.h>

void postprocess_smooth_edges(uint32_t fb_addr, int min_x, int min_y, int max_x, int max_y, uint16_t bg_color);

#endif /* OPENDC_POSTPROCESS_H */
