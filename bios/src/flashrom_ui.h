#ifndef __BIOS_FLASHROM_UI_H
#define __BIOS_FLASHROM_UI_H

#include <stdint.h>

void flashrom_ui_init(void);
void flashrom_ui_render(void);
void flashrom_ui_handle_input(uint32_t pressed);

#endif /* __BIOS_FLASHROM_UI_H */
