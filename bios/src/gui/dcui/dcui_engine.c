#include "dcui_engine.h"
#include "renderer.h"
#include "audio_driver.h"
#include "disc_service.h"
#include "sysinfo_service.h"
#include <dc/maple/controller.h>
#include <string.h>
#include <math.h>

static dcui_vm_state_t s_vm;
bios_screen_t g_next_screen_request = SCREEN_MAIN_MENU;
extern const uint8_t DEFAULT_MENU_DCUI[];

int dcui_engine_init(const uint8_t *blob) {
    const uint8_t *src = blob ? blob : DEFAULT_MENU_DCUI;
    dcui_vm_init(&s_vm, src);
    g_next_screen_request = SCREEN_MAIN_MENU;
    dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_INIT, -1);
    return 0;
}

static void project_3d_to_2d(const dcui_vec3_t *pos, int *out_x, int *out_y) {
    float cam_z = 4.5f;
    float fov_scale = 320.0f;
    float rel_z = cam_z - pos->z;
    if (rel_z < 0.1f) rel_z = 0.1f;

    *out_x = (int)(320.0f + (pos->x * fov_scale / rel_z));
    *out_y = (int)(240.0f - (pos->y * fov_scale / rel_z));
}

void dcui_engine_render(void) {
    if (!s_vm.header || !s_vm.nodes) return;

    /* Header using authentic Sega 12x24 BFont */
    draw_bfont(MARGIN_X, 24, COLOR_WHITE, "DREAMCAST 3D INTERACTIVE BIOS");
    draw_rect(MARGIN_X, 52, SCREEN_W - (MARGIN_X * 2), 1, COLOR_DARK_GRAY);

    /* Render all 3D Blender Nodes */
    for (uint32_t i = 0; i < s_vm.header->node_count; i++) {
        const dcui_binary_node_t *node = &s_vm.nodes[i];
        if (!(node->flags & DCUI_NODE_FLAG_VISIBLE)) continue;

        int sx = 0, sy = 0;
        project_3d_to_2d(&node->position, &sx, &sy);

        if (node->flags & DCUI_NODE_FLAG_INTERACTABLE) {
            int is_focused = ((int32_t)i == s_vm.current_focus_node);
            uint16_t text_color = is_focused ? COLOR_GOLD : COLOR_LIGHT_GRAY;

            int box_w = 200;
            int box_h = 36;
            int bx = sx - (box_w / 2);
            int by = sy - (box_h / 2);

            /* Selection Background Box */
            if (is_focused) {
                draw_rect(bx, by, box_w, box_h, 0x18C3); /* Subtle blue glow */
                draw_rect(bx, by, box_w, 1, COLOR_WHITE);
                draw_rect(bx, by + box_h - 1, box_w, 1, COLOR_WHITE);
                draw_rect(bx, by, 1, box_h, COLOR_WHITE);
                draw_rect(bx + box_w - 1, by, 1, box_h, COLOR_WHITE);
                draw_bfont(bx + 12, by + 6, COLOR_WHITE, ">");
            } else {
                draw_rect(bx, by, box_w, 1, COLOR_DARK_GRAY);
                draw_rect(bx, by + box_h - 1, box_w, 1, COLOR_DARK_GRAY);
                draw_rect(bx, by, 1, box_h, COLOR_DARK_GRAY);
                draw_rect(bx + box_w - 1, by, 1, box_h, COLOR_DARK_GRAY);
            }

            /* Draw Element Label using authentic 12x24 BFont */
            const char *label = node->text_binding[0] ? node->text_binding : node->name;
            draw_bfont(bx + 30, by + 6, text_color, label);
        } else if (node->type == DCUI_NODE_TEXT_ANCHOR) {
            /* Dynamic Text Overlay */
            if (strcmp(node->text_binding, "sys.status") == 0) {
                draw_sysfont(MARGIN_X, sy, COLOR_GRAY, ".// Status: OpenDC 3D Visual Blueprint Engine [ 60 FPS ]");
            }
        }
    }

    /* Centered Navigation Footer */
    draw_bfont_centered(SCREEN_W / 2, 440, COLOR_LIGHT_GRAY,
        "(A) Select   (START) Fast-Boot   (D-PAD) Move");
}

bios_screen_t dcui_engine_handle_input(uint32_t pressed) {
    g_next_screen_request = SCREEN_MAIN_MENU;

    if (!s_vm.header || !s_vm.nodes) return SCREEN_MAIN_MENU;

    int32_t cur = s_vm.current_focus_node;
    const dcui_binary_node_t *node = (cur >= 0 && cur < (int32_t)s_vm.header->node_count) ? &s_vm.nodes[cur] : NULL;

    if (pressed & CONT_DPAD_UP) {
        if (node && node->nav_up >= 0) {
            s_vm.current_focus_node = node->nav_up;
            audio_play_click();
        } else {
            /* Fallback cycle */
            for (int32_t i = cur - 1; i >= 0; i--) {
                if (s_vm.nodes[i].flags & DCUI_NODE_FLAG_INTERACTABLE) {
                    s_vm.current_focus_node = i;
                    audio_play_click();
                    break;
                }
            }
        }
    }
    if (pressed & CONT_DPAD_DOWN) {
        if (node && node->nav_down >= 0) {
            s_vm.current_focus_node = node->nav_down;
            audio_play_click();
        } else {
            /* Fallback cycle */
            for (uint32_t i = cur + 1; i < s_vm.header->node_count; i++) {
                if (s_vm.nodes[i].flags & DCUI_NODE_FLAG_INTERACTABLE) {
                    s_vm.current_focus_node = (int32_t)i;
                    audio_play_click();
                    break;
                }
            }
        }
    }
    if (pressed & CONT_DPAD_LEFT) {
        if (node && node->nav_left >= 0) {
            s_vm.current_focus_node = node->nav_left;
            audio_play_click();
        }
    }
    if (pressed & CONT_DPAD_RIGHT) {
        if (node && node->nav_right >= 0) {
            s_vm.current_focus_node = node->nav_right;
            audio_play_click();
        }
    }

    if (pressed & CONT_A) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_A, cur);
    }
    if (pressed & CONT_B) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_B, cur);
    }
    if (pressed & CONT_X) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_X, cur);
    }
    if (pressed & CONT_Y) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_Y, cur);
    }
    if (pressed & CONT_START) {
        dcui_vm_dispatch_event(&s_vm, DCUI_EVENT_ON_PRESS_START, cur);
    }

    return g_next_screen_request;
}
