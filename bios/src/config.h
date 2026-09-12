#ifndef __BIOS_CONFIG_H
#define __BIOS_CONFIG_H

#include <stdint.h>

/* Screen Dimensions */
#define SCREEN_W 640
#define SCREEN_H 480
#define MARGIN_X 44

/* RGB565 */
#define COLOR_BLACK      0x0000  /* Black */
#define COLOR_WHITE      0xFFFF  /* White */
#define COLOR_LIGHT_GRAY 0xCE79  /* Light Gray */
#define COLOR_GRAY       0x8C71  /* Developer Comment Gray */
#define COLOR_DARK_GRAY  0x4208  /* Subtle Divider Gray */
#define COLOR_GREEN      0x2FE4  /* Status Pass / Ready Green */
#define COLOR_GOLD       0xFEA0  /* Accent Gold */
#define COLOR_CYAN       0x367F  /* Header Cyan */
#define COLOR_RED        0xF986  /* Error Red */
#define COLOR_ORANGE     0xFB20  /* Dreamcast Swirl Orange */
#define COLOR_BLUE       0x1BD9  /* Cobalt Blue */


/* Screen IDs */
typedef enum {
    SCREEN_MAIN_MENU = 0,
    SCREEN_SYSINFO   = 1,
    SCREEN_MAPLE     = 2,
    SCREEN_FLASHROM  = 3,
    SCREEN_MEMORY    = 4,
    SCREEN_TEST      = 5,
    SCREEN_COUNT     = 6
} bios_screen_t;

#endif /* __BIOS_CONFIG_H */
