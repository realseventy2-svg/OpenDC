#ifndef __BIOS_SCREEN_MEMORY_H
#define __BIOS_SCREEN_MEMORY_H

#include <stdint.h>
#include "config.h"

void screen_memory_render(void);
bios_screen_t screen_memory_handle_input(uint32_t pressed);

#endif /* __BIOS_SCREEN_MEMORY_H */
