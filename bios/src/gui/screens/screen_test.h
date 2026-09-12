#ifndef __BIOS_SCREEN_TEST_H
#define __BIOS_SCREEN_TEST_H

#include <stdint.h>
#include "config.h"

void screen_test_render(void);
bios_screen_t screen_test_handle_input(uint32_t pressed);

#endif /* __BIOS_SCREEN_TEST_H */
