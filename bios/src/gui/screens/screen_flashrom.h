#ifndef __BIOS_SCREEN_FLASHROM_H
#define __BIOS_SCREEN_FLASHROM_H

#include <stdint.h>
#include "config.h"

void screen_flashrom_init(void);
void screen_flashrom_render(void);
bios_screen_t screen_flashrom_handle_input(uint32_t pressed);

#endif /* __BIOS_SCREEN_FLASHROM_H */
