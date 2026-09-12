#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/video.h>
#include <dc/sq.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

/* Backend Services */
#include "audio_driver.h"
#include "disc_service.h"
#include "flashrom_service.h"
#include "maple_service.h"
#include "memory_service.h"
#include "sysinfo_service.h"

/* GUI Render & Screens */
#include "renderer.h"
#include "screen_main_menu.h"
#include "screen_sysinfo.h"
#include "screen_maple.h"
#include "screen_flashrom.h"
#include "screen_memory.h"
#include "screen_test.h"

extern const uint8_t romdisk[];

/* Fast boot flags */
KOS_INIT_FLAGS(INIT_IRQ | INIT_THD_PREEMPT | INIT_CONTROLLER | INIT_VMU | INIT_NO_DCLOAD);

/* Framebuffer pointer */
static uint16_t *s_fb = NULL;
static bios_screen_t s_screen = SCREEN_MAIN_MENU;

/* -------------------------------------------------------------------------- */
/* Main Application Entry & Event Loop                                        */
/* -------------------------------------------------------------------------- */
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Disable KOS fb console debug prints to avoid any artifact on VRAM */
    dbgio_disable();

    /* Initialize Video Mode (640x480 RGB565 Double-Buffered) */
    vid_set_mode(DM_640x480, PM_RGB565);
    sq_set16(vram_s, 0, SCREEN_W * SCREEN_H * 2);

    /* Allocate back buffer in SDRAM */
    s_fb = (uint16_t *)malloc(SCREEN_W * SCREEN_H * 2);
    if(!s_fb) {
        s_fb = (uint16_t *)vram_s;
    }
    memset(s_fb, 0, SCREEN_W * SCREEN_H * 2);
    renderer_set_fb(s_fb);

    /* Initialize Backend Subsystems */
    audio_driver_init();
    disc_service_init();
    flashrom_service_init();
    maple_service_init();
    screen_flashrom_init();

    uint32_t prev_buttons = 0;
    int disc_probe_timer = 0;

    while(1) {
        /* Update audio decay timer */
        audio_driver_update();

        /* Periodically poll disc drive state */
        disc_probe_timer++;
        if(disc_probe_timer >= 180) {
            disc_probe_timer = 0;
            disc_service_probe();
        }

        /* Clear backbuffer to pure black */
        draw_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BLACK);

        /* Route to active screen renderer */
        switch(s_screen) {
            case SCREEN_MAIN_MENU: screen_main_menu_render(); break;
            case SCREEN_SYSINFO:   screen_sysinfo_render(); break;
            case SCREEN_MAPLE:     screen_maple_render(); break;
            case SCREEN_FLASHROM:  screen_flashrom_render(); break;
            case SCREEN_MEMORY:    screen_memory_render(); break;
            case SCREEN_TEST:      screen_test_render(); break;
            default:               s_screen = SCREEN_MAIN_MENU; break;
        }

        /* Sync with VBlank and DMA transfer backbuffer to VRAM */
        if(s_fb != (uint16_t *)vram_s) {
            vid_waitvbl();
            sq_cpy(vram_s, s_fb, SCREEN_W * SCREEN_H * 2);
        }

        /* Process Maple Controller Input */
        maple_device_t *cont = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
        if(cont) {
            cont_state_t *st = (cont_state_t *)maple_dev_status(cont);
            if(st) {
                uint32_t pressed = st->buttons & ~prev_buttons;
                prev_buttons = st->buttons;

                if(pressed) {
                    switch(s_screen) {
                        case SCREEN_MAIN_MENU:
                            s_screen = screen_main_menu_handle_input(pressed);
                            break;
                        case SCREEN_SYSINFO:
                            s_screen = screen_sysinfo_handle_input(pressed);
                            break;
                        case SCREEN_MAPLE:
                            s_screen = screen_maple_handle_input(pressed);
                            break;
                        case SCREEN_FLASHROM:
                            s_screen = screen_flashrom_handle_input(pressed);
                            break;
                        case SCREEN_MEMORY:
                            s_screen = screen_memory_handle_input(pressed);
                            break;
                        case SCREEN_TEST:
                            s_screen = screen_test_handle_input(pressed);
                            break;
                        default:
                            s_screen = SCREEN_MAIN_MENU;
                            break;
                    }
                }
            }
        }

        thd_sleep(16);
    }

    return 0;
}
