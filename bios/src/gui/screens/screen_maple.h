#ifndef __BIOS_SCREEN_MAPLE_H
#define __BIOS_SCREEN_MAPLE_H

#include <stdint.h>
#include "config.h"

void screen_maple_render(void);
bios_screen_t screen_maple_handle_input(uint32_t pressed);

#endif /* __BIOS_SCREEN_MAPLE_H */
