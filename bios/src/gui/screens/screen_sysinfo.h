#ifndef __BIOS_SCREEN_SYSINFO_H
#define __BIOS_SCREEN_SYSINFO_H

#include <stdint.h>
#include "config.h"

void screen_sysinfo_render(void);
bios_screen_t screen_sysinfo_handle_input(uint32_t pressed);

#endif /* __BIOS_SCREEN_SYSINFO_H */
