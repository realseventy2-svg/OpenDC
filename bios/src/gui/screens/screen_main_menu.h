#ifndef __BIOS_SCREEN_MAIN_MENU_H
#define __BIOS_SCREEN_MAIN_MENU_H

#include <stdint.h>
#include "config.h"

void screen_main_menu_render(void);
bios_screen_t screen_main_menu_handle_input(uint32_t pressed);

#endif /* __BIOS_SCREEN_MAIN_MENU_H */
