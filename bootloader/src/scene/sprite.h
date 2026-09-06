#ifndef OPENDC_SPRITE_H
#define OPENDC_SPRITE_H

#include <stdint.h>

void sprite_blit_argb4444(uint32_t fb_addr,
                          int dest_x,
                          int dest_y,
                          int width,
                          int height,
                          const uint16_t *pixels,
                          int global_alpha);

#endif /* OPENDC_SPRITE_H */
