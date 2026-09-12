#ifndef __DCUI_ENGINE_H
#define __DCUI_ENGINE_H

#include "dcui_types.h"
#include "dcui_vm.h"
#include "config.h"

int  dcui_engine_init(const uint8_t *blob);
void dcui_engine_render(void);
bios_screen_t dcui_engine_handle_input(uint32_t pressed);

#endif /* __DCUI_ENGINE_H */
